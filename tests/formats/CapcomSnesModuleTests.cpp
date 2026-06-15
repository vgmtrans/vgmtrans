/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesModule.h"

#include "value/export/Export.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/formats/CapcomSnes/CapcomSnesSequence.h"
#include "value/formats/CapcomSnes/CapcomSnesSynth.h"
#include "value/formats/ValueFormats.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::capcom_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value & 0xff);
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeCapcomSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 16> readBgmAddressPattern{0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
                                                     0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
  writeBytes(bytes, 0x0500, readBgmAddressPattern);
  bytes[0x0500 + 5] = 0x20;
  bytes[0x0500 + 8] = 0x00;

  constexpr std::array<u8, 12> loadInstrTablePattern{0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60,
                                                     0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};
  writeBytes(bytes, 0x0600, loadInstrTablePattern);
  bytes[0x0600 + 7] = 0x00;
  bytes[0x0600 + 10] = 0x40;

  constexpr std::array<u8, 16> dspRegInitPattern{0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
                                                 0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
  writeBytes(bytes, 0x0700, dspRegInitPattern);
  bytes[0x0700 + 1] = 1;
  writeLe16(bytes, 0x0700 + 3, 0x0800);
  writeLe16(bytes, 0x0700 + 9, 0x0810);
  bytes[0x0801] = 0x5d;
  bytes[0x0811] = 0x50;

  for (int track = 0; track < 8; ++track) {
    writeBe16(bytes, 0x2001 + track * 2, 0x3000);
  }

  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x34;
  bytes[0x3003] = 0x08;
  bytes[0x3004] = 0x00;
  bytes[0x3005] = 0x07;
  bytes[0x3006] = 0x40;
  bytes[0x3007] = 0x18;
  bytes[0x3008] = 0x00;
  bytes[0x3009] = 0x1a;
  bytes[0x300a] = 0x00;
  bytes[0x300b] = 0x20;
  bytes[0x300c] = 0x1a;
  bytes[0x300d] = 0x02;
  bytes[0x300e] = 0x20;
  bytes[0x300f] = 0x41;
  bytes[0x3010] = 0x17;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x8f;
  bytes[0x4002] = 0xe0;
  bytes[0x4003] = 0x00;
  writeBe16(bytes, 0x4004, 0x0100);

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

std::vector<u8> makeCapcomSnesSpc() {
  std::vector<u8> bytes(0x10180);
  constexpr std::string_view signature = "SNES-SPC700 Sound File Data";
  std::ranges::copy(signature, bytes.begin());
  bytes[0x21] = 0x1a;
  bytes[0x22] = 0x1a;
  bytes[0x23] = 0x1a;
  bytes[0x24] = 0x30;
  constexpr std::string_view title = "Capcom Logo";
  std::ranges::copy(title, bytes.begin() + 0x2e);

  const auto aram = makeCapcomSnesAram();
  std::ranges::copy(aram, bytes.begin() + 0x100);
  return bytes;
}

}  // namespace

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  expect(session.dialects().contains("capcom-snes:v3"),
         "value format registration should include CapcomSnes sequence dialects");
  session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesAram());

  const SessionSnapshot project = session.scanPendingSources();
  expect(project.diagnostics.empty(), "CapcomSnes scan should not report diagnostics for complete fixture");
  expect(project.collections.size() == 1, "CapcomSnes scan should produce one collection");
  expect(project.assets.size() == 3, "CapcomSnes scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first CapcomSnes asset should be sequence");
  expect(sequence->metadata.format == "CapcomSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should point at fixed BGM header body");
  expect(sequence->program.dialect.value == "capcom-snes:v3", "sequence should carry the detected CapcomSnes dialect");
  expect(sequence->program.timebase.ppqn == 48, "sequence should use CapcomSnes PPQN");
  expect(sequence->program.behavior.defaultLoopPolicy == LoopPolicy::PlayOnce,
         "sequence should carry CapcomSnes default loop policy");
  expect(sequence->program.tracks.size() == 8, "sequence should decode all nonzero track pointers");

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "registered dialect should interpret the scanned sequence program");
  const auto& firstTrack = sequence->program.tracks[0];
  expect(firstTrack.commands.size() == 8, "track should decode all fixture commands");
  expect(dialect->describe(firstTrack, firstTrack.commands[0]).detailKind == "capcom-snes.tempo",
         "track should decode tempo command");
  expect(
      firstTrack.bytesFor(firstTrack.commands[0])[1] == 0x12 && firstTrack.bytesFor(firstTrack.commands[0])[2] == 0x34,
      "tempo command should preserve raw big-endian value");
  expect(dialect->describe(firstTrack, firstTrack.commands[1]).detailKind == "capcom-snes.program",
         "track should decode program command");
  expect(dialect->describe(firstTrack, firstTrack.commands[2]).detailKind == "capcom-snes.volume",
         "track should decode volume command");
  expect(dialect->describe(firstTrack, firstTrack.commands[3]).detailKind == "capcom-snes.pan",
         "track should decode pan command");
  expect(dialect->describe(firstTrack, firstTrack.commands[4]).detailKind == "capcom-snes.lfo",
         "track should decode vibrato/LFO command");
  expect(dialect->describe(firstTrack, firstTrack.commands[5]).detailKind == "capcom-snes.lfo",
         "track should decode modulation-rate/LFO command");
  expect(dialect->describe(firstTrack, firstTrack.commands[6]).detailKind == "capcom-snes.note",
         "track should decode note command");
  expect(dialect->describe(firstTrack, firstTrack.commands[7]).detailKind == "capcom-snes.end",
         "track should decode end command");
  expect(sequence->program.referencedInstruments.size() == 1, "sequence should expose unique referenced instruments");
  expect(
      sequence->program.referencedInstruments[0].bank == 0 && sequence->program.referencedInstruments[0].program == 0,
      "instrument reference should preserve decoded bank and program");
  expect(sequence->program.referencedInstruments[0].asset == project.collections[0].instrumentSets[0],
         "instrument reference should point at the decoded instrument set asset");
  expect(sequence->program.referencedInstruments[0].range.has_value() &&
             sequence->program.referencedInstruments[0].range->offset == 0x3003 &&
             sequence->program.referencedInstruments[0].range->size == 2,
         "instrument reference should preserve the program command source range");

  const auto& sequenceItems = sequence->metadata.items.nodes;
  const auto commandItemCount =
      std::ranges::count_if(sequenceItems, [](const ItemNode& item) { return item.kind == ItemKind::Command; });
  expect(commandItemCount == sequence->program.tracks.size() * sequence->program.tracks[0].commands.size(),
         "sequence item tree should expose decoded command nodes for every track");

  const auto firstTrackItem =
      std::ranges::find_if(sequenceItems, [](const ItemNode& item) { return item.kind == ItemKind::Track; });
  expect(firstTrackItem != sequenceItems.end(), "sequence item tree should expose track nodes");
  expect(firstTrackItem->children.size() == sequence->program.tracks[0].commands.size(),
         "track item should parent its decoded command nodes");

  const auto firstTempoItem = std::ranges::find_if(sequenceItems, [](const ItemNode& item) {
    return item.kind == ItemKind::Command && item.detailKind == "capcom-snes.tempo";
  });
  expect(firstTempoItem != sequenceItems.end(), "sequence item tree should expose typed command nodes");
  expect(firstTempoItem->parent == firstTrackItem->id, "command item should point back to its track item");
  expect(firstTempoItem->name == "Tempo", "command item should carry a readable command name");
  expect(firstTempoItem->description == "raw 4660, microseconds_per_quarter 42191",
         "command item should preserve raw and interpreted command values");
  expect(firstTempoItem->range.offset == 0x3000 && firstTempoItem->range.size == 3,
         "command item should preserve command source range");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
  expect(midiSequence.diagnostics.empty(), "CapcomSnes MIDI sequence build should not warn for linear fixture");
  expect(midiSequence.tracks.size() == 8, "builder should preserve track count");
  expect(midiSequence.tracks[0].events.size() == 15,
         "built track should include port, initial, command, and end events");
  expect(std::get<MidiPort>(midiSequence.tracks[0].events[0]).port == 0,
         "CapcomSnes should emit the legacy MIDI port metadata");
  expect(std::get<Reverb>(midiSequence.tracks[0].events[1]).value == 0,
         "CapcomSnes should emit the legacy initial reverb controller");
  expect(std::get<MonoMode>(midiSequence.tracks[0].events[2]).channels == 0,
         "CapcomSnes should emit the legacy initial mono-mode controller");
  expect(std::get<Tempo>(midiSequence.tracks[0].events[3]).microsecondsPerQuarter == 42191,
         "CapcomSnes source command should interpret tempo with driver timing math");
  expect(std::holds_alternative<BankSelect>(midiSequence.tracks[0].events[4]),
         "CapcomSnes source command should force bank select like the legacy converter");
  expect(std::holds_alternative<ProgramChange>(midiSequence.tracks[0].events[5]),
         "CapcomSnes source command should emit program changes");
  expect(std::holds_alternative<Volume14>(midiSequence.tracks[0].events[6]),
         "CapcomSnes source command should emit high-resolution target-quantized volume");
  expect(std::get<Pan>(midiSequence.tracks[0].events[7]).value == 64,
         "CapcomSnes center pan should map to MIDI center pan");
  expect(std::holds_alternative<Expression>(midiSequence.tracks[0].events[8]),
         "CapcomSnes pan should emit expression compensation for the source pan law");
  expect(std::get<VibratoDepth>(midiSequence.tracks[0].events[9]).value == 0,
         "CapcomSnes vibrato depth should stay silent until the LFO rate enables output");
  expect(std::get<VibratoDepth>(midiSequence.tracks[0].events[10]).value == 32,
         "CapcomSnes LFO rate should enable the latched vibrato depth");
  expect(std::holds_alternative<VibratoFrequency>(midiSequence.tracks[0].events[11]),
         "CapcomSnes LFO rate should emit vibrato frequency");
  expect(std::holds_alternative<TremoloFrequency>(midiSequence.tracks[0].events[12]),
         "CapcomSnes LFO rate should emit tremolo frequency");
  expect(std::get<NoteDuration>(midiSequence.tracks[0].events[13]).duration == 6,
         "CapcomSnes note length index should map to ticks");
  expect(std::get<EndOfTrack>(midiSequence.tracks[0].events[14]).tick == 6,
         "builder should advance time before end of track");

  const auto artifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                 .kinds = {ExportKind::Midi},
                                                                                 .loopPolicy = LoopPolicy::PlayOnce,
                                                                             });
  expect(artifacts.size() == 1, "value export should produce one MIDI artifact");
  expect(artifacts[0].filename == "Mega Man X.mid", "MIDI artifact should use collection name");
  expect(artifacts[0].mediaType == "audio/midi", "MIDI artifact should use audio/midi media type");
  expect(artifacts[0].diagnostics.empty(), "MIDI artifact should not carry diagnostics for linear fixture");
  expect(artifacts[0].bytes == MidiExporter().exportMidi(midiSequence),
         "Session MIDI export should match direct builder/exporter output");

  const auto wavArtifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                    .kinds = {ExportKind::Wav},
                                                                                });
  expect(wavArtifacts.size() == 1, "value export should produce one WAV artifact for one sample");
  expect(wavArtifacts[0].filename == "Mega Man X-0-Sample 0.wav", "WAV artifact should include sample index and name");
  expect(wavArtifacts[0].mediaType == "audio/wav", "WAV artifact should use audio/wav media type");
  expect(wavArtifacts[0].diagnostics.empty(), "WAV artifact should not carry diagnostics for decodable sample");
  expect(wavArtifacts[0].bytes.size() == 76, "one BRR block should export as 44-byte header plus 32 PCM bytes");
  expect(std::vector<u8>(wavArtifacts[0].bytes.begin(), wavArtifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "WAV artifact should start with a RIFF header");
  expect(wavArtifacts[0].bytes[24] == 0x00 && wavArtifacts[0].bytes[25] == 0x7d,
         "WAV artifact should preserve the CapcomSnes sample rate");

  const auto sf2Artifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                    .kinds = {ExportKind::SoundFont2},
                                                                                });
  expect(sf2Artifacts.size() == 1, "value export should produce one SoundFont artifact");
  expect(sf2Artifacts[0].filename == "Mega Man X.sf2", "SoundFont artifact should use collection name");
  expect(sf2Artifacts[0].mediaType == "audio/soundfont", "SoundFont artifact should use audio/soundfont media type");
  expect(sf2Artifacts[0].diagnostics.empty(), "SoundFont artifact should not carry diagnostics for complete fixture");
  expect(sf2Artifacts[0].bytes.size() > 44, "SoundFont artifact should contain RIFF bytes");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin(), sf2Artifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont artifact should start with a RIFF header");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin() + 8, sf2Artifacts[0].bytes.begin() + 12) ==
             std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont artifact should use sfbk RIFF type");

  const auto dlsArtifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                    .kinds = {ExportKind::Dls},
                                                                                });
  expect(dlsArtifacts.size() == 1, "value export should produce one DLS artifact");
  expect(dlsArtifacts[0].filename == "Mega Man X.dls", "DLS artifact should use collection name");
  expect(dlsArtifacts[0].mediaType == "audio/dls", "DLS artifact should use audio/dls media type");
  expect(dlsArtifacts[0].diagnostics.empty(), "DLS artifact should not carry diagnostics for complete fixture");
  expect(dlsArtifacts[0].bytes.size() > 44, "DLS artifact should contain RIFF bytes");
  expect(std::vector<u8>(dlsArtifacts[0].bytes.begin(), dlsArtifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "DLS artifact should start with a RIFF header");
  expect(std::vector<u8>(dlsArtifacts[0].bytes.begin() + 8, dlsArtifacts[0].bytes.begin() + 12) ==
             std::vector<u8>{'D', 'L', 'S', ' '},
         "DLS artifact should use DLS RIFF type");

  const auto* instruments = std::get_if<InstrumentSetAsset>(&project.assets[1]);
  expect(instruments != nullptr, "second CapcomSnes asset should be instrument set");
  expect(instruments->instruments.size() == 1, "instrument set should parse one valid instrument");
  const auto& instrument = instruments->instruments[0];
  expect(instrument.program == 0, "instrument program should match table index");
  expect(instrument.range.offset == 0x4000 && instrument.range.size == 6,
         "instrument should preserve the table entry source range");
  expect(instrument.regions.size() == 1, "instrument should expose one region");
  expect(instrument.regions[0].range.offset == 0x4000 && instrument.regions[0].range.size == 6,
         "region should preserve the instrument header source range");
  const auto& envelope = instrument.regions[0].envelope;
  expect(envelope.attack == 63, "instrument envelope should convert SNES attack to microseconds");
  expect(envelope.decay == kEnvelopeInfinite, "instrument envelope should preserve infinite SNES sustain decay");
  expect(envelope.sustain == 1000, "instrument envelope should convert SNES sustain to a linear amplitude level");
  expect(envelope.release == 0, "instrument envelope should match Capcom legacy gain-based release handling");
  expect(instrument.generators.size() == 2, "instrument should carry legacy modulation generator settings");
  expect(
      instrument.generators[0].destination == SynthDestination::VibratoRate && instrument.generators[0].amount == -8479,
      "instrument should carry legacy vibrato base frequency");
  expect(
      instrument.generators[1].destination == SynthDestination::TremoloRate && instrument.generators[1].amount == -7279,
      "instrument should carry legacy tremolo base frequency");
  expect(instrument.modulators.size() == 6, "instrument should carry legacy synth modulators");
  expect(instrument.modulators[0].source == SynthSource::ChannelPressure &&
             instrument.modulators[0].destination == SynthDestination::VibratoDepth &&
             instrument.modulators[0].amount == 0,
         "instrument should nullify channel-pressure vibrato depth like legacy export");
  expect(!instrument.modulators[1].source && instrument.modulators[1].destination == SynthDestination::VibratoDepth &&
             instrument.modulators[1].amount == 1200,
         "instrument should carry legacy vibrato depth range");
  expect(!instrument.modulators[2].source && instrument.modulators[2].destination == SynthDestination::VibratoRate &&
             instrument.modulators[2].amount == 9669,
         "instrument should carry legacy vibrato rate range");
  expect(!instrument.modulators[3].source && instrument.modulators[3].destination == SynthDestination::TremoloRate &&
             instrument.modulators[3].amount == 9669,
         "instrument should carry legacy tremolo rate range");
  expect(!instrument.modulators[4].source && instrument.modulators[4].destination == SynthDestination::TremoloDepth &&
             instrument.modulators[4].amount == 484,
         "instrument should carry legacy tremolo depth range");
  expect(!instrument.modulators[5].source && instrument.modulators[5].destination == SynthDestination::Volume &&
             instrument.modulators[5].amount == 484,
         "instrument should carry legacy no-boost attenuation modulator");

  const auto& instrumentItems = instruments->metadata.items.nodes;
  const auto instrumentItem =
      std::ranges::find_if(instrumentItems, [](const ItemNode& item) { return item.kind == ItemKind::Instrument; });
  expect(instrumentItem != instrumentItems.end(), "instrument set item tree should expose instrument nodes");
  const auto regionItem = std::ranges::find_if(instrumentItems, [](const ItemNode& item) {
    return item.kind == ItemKind::Region && item.detailKind == "capcom-snes-region";
  });
  expect(regionItem != instrumentItems.end(), "instrument set item tree should expose region nodes");
  expect(regionItem->parent == instrumentItem->id, "region item should point back to its instrument item");
  expect(regionItem->range.offset == 0x4000 && regionItem->range.size == 6,
         "region item should preserve the instrument header source range");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets[2]);
  expect(samples != nullptr, "third CapcomSnes asset should be sample collection");
  expect(samples->samples.samples.size() == 1, "sample collection should include referenced sample");
  expect(samples->samples.samples[0].codec == AudioCodec::SnesBrr, "sample should preserve BRR codec");
  expect(samples->samples.samples[0].encodedData.offset == 0x6000, "sample should point at encoded BRR bytes");
  expect(samples->samples.samples[0].encodedData.size == 9, "sample should preserve encoded BRR byte length");

  const auto& sampleItems = samples->metadata.items.nodes;
  const auto sampleCollectionItem = std::ranges::find_if(sampleItems, [](const ItemNode& item) {
    return item.kind == ItemKind::SampleCollection && item.detailKind == "snes-sample-dir";
  });
  expect(sampleCollectionItem != sampleItems.end(), "sample collection item tree should expose the sample DIR root");
  expect(sampleCollectionItem->range.offset == 0x5000 && sampleCollectionItem->range.size == 4,
         "sample collection root should preserve the DIR table source range");
  const auto sampleItem = std::ranges::find_if(sampleItems, [](const ItemNode& item) {
    return item.kind == ItemKind::Sample && item.detailKind == "snes-brr-sample";
  });
  expect(sampleItem != sampleItems.end(), "sample collection item tree should expose sample nodes");
  expect(sampleItem->parent == sampleCollectionItem->id, "sample item should point back to the sample collection root");
  expect(sampleItem->range.offset == 0x6000 && sampleItem->range.size == 9,
         "sample item should preserve the encoded BRR source range");
  expect(sampleItem->description == "DIR entry $5000", "sample item should retain its source DIR entry address");

  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence");
  expect(project.collections[0].instrumentSets == std::vector<AssetId>{instruments->metadata.id},
         "collection should reference instrument set");
  expect(project.collections[0].sampleCollections == std::vector<AssetId>{samples->metadata.id},
         "collection should reference sample collection");
}

void capcomSnesModuleScansSpcThroughVirtualAramSource() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const auto sourceId = session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesSpc());

  const SessionSnapshot project = session.scanPendingSources();
  expect(project.diagnostics.empty(), "SPC-backed CapcomSnes scan should not report diagnostics");
  expect(project.sources.size() == 2, "SPC scan should preserve original source plus extracted ARAM");
  expect(!project.sources[0].derived(), "original SPC source should not be derived");
  expect(project.sources[1].derived(), "SPC RAM source should be derived");
  expect(project.sources[1].name == "Mega Man X.spc - ram", "derived ARAM source should match legacy naming");
  expect(project.sources[1].title == "Capcom Logo", "derived ARAM source should carry the SPC title tag");
  expect(project.sources[1].origin.has_value(), "derived ARAM source should preserve origin range");
  expect(project.sources[1].origin->source == sourceId, "derived ARAM origin should point at the SPC source");
  expect(project.sources[1].origin->offset == 0x100 && project.sources[1].origin->size == 0x10000,
         "derived ARAM origin should point at SPC RAM bytes");

  expect(project.collections.size() == 1, "SPC-backed scan should produce one collection");
  expect(project.collections[0].name == "Capcom Logo", "SPC-backed collection should use the SPC title tag");
  expect(project.assets.size() == 3, "SPC-backed scan should produce CapcomSnes assets from derived ARAM");
  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets[0]);
  expect(sequence != nullptr, "SPC-backed scan should produce a sequence");
  expect(sequence->metadata.name == "Capcom Logo", "SPC-backed sequence should use the SPC title tag");
  expect(sequence->metadata.range.source == SourceId{1}, "sequence range should point at derived ARAM source");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should preserve ARAM-relative address");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets[2]);
  expect(samples != nullptr, "SPC-backed scan should produce samples");
  expect(!samples->samples.samples.empty(), "SPC-backed scan should discover sample data");
  expect(samples->samples.samples[0].encodedData.source == SourceId{1},
         "sample encoded data should point at derived ARAM source");
}

void capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy() {
  auto bytes = makeCapcomSnesAram();
  bytes[0x400c] = 0x00;
  bytes[0x400d] = 0x8f;
  bytes[0x400e] = 0xe0;
  bytes[0x400f] = 0x00;
  writeBe16(bytes, 0x4010, 0x0200);

  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "blank-terminated.spc"}, std::move(bytes));
  const auto infos = parseCapcomSnesInstrumentInfos(sources.reader(sourceId), 0x4000, 0x5000);
  expect(infos.size() == 2, "CapcomSnes instrument parsing should skip blank table slots like legacy");
  expect(infos[0].index == 0 && infos[1].index == 2,
         "CapcomSnes instrument parsing should preserve sparse instrument indexes");

  std::vector<u8> fullTable(0x10000);
  writeLe16(fullTable, 0x5000, 0x6000);
  writeLe16(fullTable, 0x5002, 0x6000);
  fullTable[0x6000] = 0x01;
  for (u32 index = 0; index <= 0x80; ++index) {
    const size_t address = 0x3000 + index * 6;
    fullTable[address] = 0x00;
    fullTable[address + 1] = 0x8f;
    fullTable[address + 2] = 0xe0;
    fullTable[address + 3] = 0x00;
    writeBe16(fullTable, address + 4, 0x0100);
  }

  SourceStore limitSources;
  const auto limitSourceId = limitSources.add(SourceFile{.name = "program-limit.spc"}, std::move(fullTable));
  const auto limitedInfos = parseCapcomSnesInstrumentInfos(limitSources.reader(limitSourceId), 0x3000, 0x5000);
  expect(limitedInfos.size() == 0x81, "CapcomSnes instrument parsing should match legacy banked program scanning");
  expect(limitedInfos.back().index == 0x80, "CapcomSnes instrument parsing should emit bank-1 programs");
}

void capcomSnesNoteStateCommandsAreTypedAndInterpreted() {
  auto bytes = makeCapcomSnesAram();
  bytes[0x3000] = 0x09;
  bytes[0x3001] = 0x04;
  bytes[0x3002] = 0x04;
  bytes[0x3003] = 0x48;
  bytes[0x3004] = 0x41;
  bytes[0x3005] = 0x17;

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = "Mega Man X.spc"}, std::move(bytes));

  const SessionSnapshot project = session.scanPendingSources();
  expect(project.diagnostics.empty(), "CapcomSnes note-state scan should not report diagnostics");
  expect(!project.assets.empty(), "CapcomSnes note-state scan should produce assets");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets[0]);
  expect(sequence != nullptr, "CapcomSnes note-state scan should produce a sequence");
  expect(!sequence->program.tracks.empty(), "CapcomSnes note-state scan should decode tracks");

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "CapcomSnes note-state scan should have a registered dialect");
  const auto& track = sequence->program.tracks[0];
  const auto& commands = track.commands;
  expect(commands.size() == 4, "CapcomSnes note-state fixture should decode four commands");

  const CommandInfo octave = dialect->describe(track, commands[0]);
  expect(octave.detailKind == "capcom-snes.octave", "CapcomSnes octave opcode should decode as a local command");
  expect(track.operandsFor(commands[0]).size() == 1 && std::get<u64>(track.operandsFor(commands[0])[0].value) == 4,
         "CapcomSnes octave command should preserve its raw octave operand");
  expect(commands[0].range.offset == 0x3000 && commands[0].range.size == 2,
         "CapcomSnes octave command should preserve its source range");

  const CommandInfo attributes = dialect->describe(track, commands[1]);
  expect(attributes.detailKind == "capcom-snes.note-attributes",
         "CapcomSnes attributes opcode should decode as a local command");
  expect(track.operandsFor(commands[1]).size() == 1 && std::get<u64>(track.operandsFor(commands[1])[0].value) == 0x48,
         "CapcomSnes note attributes should preserve their raw attribute byte");
  expect(commands[1].range.offset == 0x3002 && commands[1].range.size == 2,
         "CapcomSnes note attributes should preserve their source range");

  const auto attributeItem = std::ranges::find_if(sequence->metadata.items.nodes, [](const ItemNode& item) {
    return item.kind == ItemKind::Command && item.detailKind == "capcom-snes.note-attributes" &&
           item.range.offset == 0x3002;
  });
  expect(attributeItem != sequence->metadata.items.nodes.end(),
         "CapcomSnes item tree should expose typed note-attribute command nodes");
  expect(attributeItem->name == "Note Attributes", "note-attribute item should carry a readable name");
  expect(attributeItem->description == "raw 72", "note-attribute item should preserve raw command values");

  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
  expect(midiSequence.diagnostics.empty(), "CapcomSnes note-state emission should not report diagnostics");
  expect(!midiSequence.tracks.empty(), "CapcomSnes note-state emission should preserve tracks");

  const auto& events = midiSequence.tracks[0].events;
  const auto note = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* typed = std::get_if<NoteDuration>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(note != events.end(), "CapcomSnes note-state fixture should emit a note");
  expect(std::get<NoteDuration>(*note).key == 72,
         "CapcomSnes note-state emission should apply octave and 2-octave-up attributes");
  expect(std::get<NoteDuration>(*note).duration == 7,
         "CapcomSnes slurred note-state emission should preserve legacy note extension");
  expect(std::get<EndOfTrack>(events.back()).tick == 6,
         "CapcomSnes note-state emission should still advance by the decoded note length");
}

void capcomSnesSourceDialectDecodesAndRendersDriverCommands() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x00;
  bytes[0x3003] = 0x08;
  bytes[0x3004] = 0x85;
  bytes[0x3005] = 0x07;
  bytes[0x3006] = 0x80;
  bytes[0x3007] = 0x04;
  bytes[0x3008] = 0x10;
  bytes[0x3009] = 0x64;
  bytes[0x300a] = 0x18;
  bytes[0x300b] = 0x00;
  bytes[0x300c] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 2, 0x3000);
  expect(track.commands.size() == 7,
         "CapcomSnes source dialect should decode the fixture commands, got " + std::to_string(track.commands.size()));
  expect(track.addressIndex.find(Address{0x3009}).has_value(),
         "CapcomSnes source dialect should index decoded command addresses");

  const auto programOperands = track.operandsFor(track.commands[1]);
  expect(programOperands.size() == 1 && programOperands[0].name == "raw" &&
             std::get<u64>(programOperands[0].value) == 0x85,
         "CapcomSnes source command should preserve decoded program operands");

  const CommandInfo programInfo = dialect.describe(track, track.commands[1]);
  expect(programInfo.name == "Program", "CapcomSnes dialect should describe commands through local command code");
  expect(programInfo.fields.size() == 3 && programInfo.fields[0].name == "raw" &&
             programInfo.fields[0].value == "133" && programInfo.fields[1].value == "1" &&
             programInfo.fields[2].value == "5",
         "CapcomSnes program display should show the raw byte plus decoded bank and program");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes source dialect fixture should render without diagnostics");
  expect(performance.tracks.size() == 1, "CapcomSnes source dialect fixture should render one track");
  expect(performance.tracks[0].endTick == 18,
         "CapcomSnes source dialect should apply one-shot dotted timing before the end command");

  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* typed = std::get_if<NotePerformanceEvent>(&event);
    return typed != nullptr;
  });
  expect(note != performance.tracks[0].events.end(), "CapcomSnes source dialect should emit a note event");
  const auto& noteEvent = std::get<NotePerformanceEvent>(*note);
  expect(noteEvent.key == 3.0 && noteEvent.durationTicks == 18,
         "CapcomSnes note event should reflect source key and dotted duration");
  expect(noteEvent.header.sourceCommand == CommandId{4} && noteEvent.header.tick == 0,
         "CapcomSnes note event should link back to the source command");

  const auto pan = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<PanPerformanceEvent>(event);
  });
  expect(pan != performance.tracks[0].events.end(), "CapcomSnes pan command should emit a target-neutral pan event");
  expect(std::get<PanPerformanceEvent>(*pan).header.tick == 18,
         "CapcomSnes pan event should occur after the note advances the VM clock");
}

void capcomSnesPanPerformanceCarriesGainCompensation() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x18;
  bytes[0x3001] = 0x40;
  bytes[0x3002] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 0, 0x3000);
  expect(track.commands.size() == 2, "CapcomSnes pan fixture should decode pan and end");

  const CommandInfo panInfo = dialect.describe(track, track.commands[0]);
  expect(panInfo.fields.size() == 3 && panInfo.fields[2].name == "linear_gain",
         "CapcomSnes pan display should expose the source pan law gain compensation");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes pan fixture should render without diagnostics");
  expect(performance.tracks[0].events.size() == 3,
         "CapcomSnes pan fixture should emit initial defaults and one pan event");
  const auto* performancePan = std::get_if<PanPerformanceEvent>(&performance.tracks[0].events[2]);
  expect(performancePan != nullptr && performancePan->linearGain < 1.0,
         "CapcomSnes pan performance should retain target-neutral gain compensation");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  expect(midi.tracks[0].events.size() == 6,
         "CapcomSnes compensated pan should render port, initial defaults, pan, expression, and end");
  expect(std::get<Pan>(midi.tracks[0].events[3]).value == 113,
         "CapcomSnes pan renderer should emit the driver-computed MIDI pan");
  expect(std::get<Expression>(midi.tracks[0].events[4]).value == 123,
         "CapcomSnes pan renderer should quantize the source gain compensation as expression");
}

void capcomSnesDialectEmitsSourceOnlyDriverSemantics() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x0c;
  bytes[0x3001] = 0x80;
  bytes[0x3002] = 0x0d;
  bytes[0x3003] = 0x20;
  bytes[0x3004] = 0x19;
  bytes[0x3005] = 0x40;
  bytes[0x3006] = 0x1b;
  bytes[0x3007] = 0x01;
  bytes[0x3008] = 0x02;
  bytes[0x3009] = 0x1c;
  bytes[0x300a] = 0x01;
  bytes[0x300b] = 0x1d;
  bytes[0x300c] = 0x05;
  bytes[0x300d] = 0x1e;
  bytes[0x300e] = 0x1f;
  bytes[0x300f] = 0x41;
  bytes[0x3010] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 0, 0x3000);
  expect(track.commands.size() == 10, "CapcomSnes source-only commands should not truncate track decoding");

  const std::vector<std::string> expectedKinds{
      "capcom-snes.tuning",        "capcom-snes.portamento-time",
      "capcom-snes.master-volume", "capcom-snes.echo-param",
      "capcom-snes.echo-on-off",   "capcom-snes.release-rate",
      "capcom-snes.nop",           "capcom-snes.nop",
      "capcom-snes.note",          "capcom-snes.end",
  };
  for (size_t index = 0; index < expectedKinds.size(); ++index) {
    expect(dialect.describe(track, track.commands[index]).detailKind == expectedKinds[index],
           "CapcomSnes source-only fixture should decode typed command " + std::to_string(index));
  }

  const auto tuningOperands = track.operandsFor(track.commands[0]);
  expect(tuningOperands.size() == 1 && tuningOperands[0].name == "tuning" &&
             std::get<s64>(tuningOperands[0].value) == -128,
         "CapcomSnes tuning command should preserve its signed operand");
  const CommandInfo release = dialect.describe(track, track.commands[5]);
  expect(release.fields.size() == 2 && release.fields[1].name == "gain" && release.fields[1].value == "165",
         "CapcomSnes release command should describe the driver GAIN value");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes source-only commands should render without diagnostics");
  expect(performance.tracks[0].events.size() == 6,
         "CapcomSnes source-only commands should emit semantic performance events where possible");
  expect(std::holds_alternative<ReverbPerformanceEvent>(performance.tracks[0].events[0]),
         "CapcomSnes should emit initial reverb before source command events");
  expect(std::holds_alternative<MonoModePerformanceEvent>(performance.tracks[0].events[1]),
         "CapcomSnes should emit initial mono mode before source command events");
  expect(std::holds_alternative<TuningPerformanceEvent>(performance.tracks[0].events[2]),
         "CapcomSnes tuning should emit a target-neutral tuning event");
  expect(std::holds_alternative<MasterLevelPerformanceEvent>(performance.tracks[0].events[3]),
         "CapcomSnes master volume should emit a target-neutral master level event");
  expect(std::holds_alternative<ReverbPerformanceEvent>(performance.tracks[0].events[4]),
         "CapcomSnes echo on/off should emit a target-neutral reverb event");
  expect(std::holds_alternative<NotePerformanceEvent>(performance.tracks[0].events[5]),
         "CapcomSnes source-only fixture should still reach the later note");
  expect(performance.tracks[0].endTick == 6, "CapcomSnes source-only fixture should advance through the later note");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  expect(std::holds_alternative<FineTune>(midi.tracks[0].events[3]),
         "CapcomSnes tuning performance should render as MIDI fine tuning");
  expect(std::holds_alternative<MasterVolume>(midi.tracks[0].events[4]),
         "CapcomSnes master level performance should render as MIDI master volume");
  expect(std::get<Reverb>(midi.tracks[0].events[5]).value == 40,
         "CapcomSnes reverb performance should preserve the legacy echo send");
}

void capcomSnesDialectEmitsPortamentoFromPreviousSourceKey() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x0d;
  bytes[0x3001] = 0x40;
  bytes[0x3002] = 0x41;
  bytes[0x3003] = 0x46;
  bytes[0x3004] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 0, 0x3000);
  expect(track.commands.size() == 4, "CapcomSnes portamento fixture should decode portamento, notes, and end");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes portamento fixture should render without diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "CapcomSnes portamento fixture should emit initial defaults, two notes, and one portamento event");
  expect(std::holds_alternative<NotePerformanceEvent>(performance.tracks[0].events[2]),
         "CapcomSnes portamento fixture should emit the first note before portamento");
  const auto* portamento = std::get_if<PortamentoPerformanceEvent>(&performance.tracks[0].events[3]);
  expect(portamento != nullptr && portamento->timeMilliseconds == 160.0 && portamento->previousKey == 0.0,
         "CapcomSnes portamento should use source-key distance and previous source key");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  expect(std::holds_alternative<NoteDuration>(midi.tracks[0].events[3]),
         "CapcomSnes portamento fixture should render the first note");
  expect(std::get<PortamentoTime14>(midi.tracks[0].events[4]).value == 160,
         "CapcomSnes portamento performance should render as 14-bit MIDI portamento time");
  expect(std::get<PortamentoControl>(midi.tracks[0].events[5]).key == 0,
         "CapcomSnes portamento performance should render the previous-key controller");
  expect(std::holds_alternative<NoteDuration>(midi.tracks[0].events[6]),
         "CapcomSnes portamento fixture should render the second note after portamento controllers");
}

void capcomSnesDialectExecutesRepeatUntilCommand() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x41;
  bytes[0x3001] = 0x0e;
  bytes[0x3002] = 0x02;
  bytes[0x3003] = 0x30;
  bytes[0x3004] = 0x00;
  bytes[0x3005] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 0, 0x3000);
  expect(track.commands.size() == 3, "CapcomSnes repeat fixture should decode note, repeat, and end");

  const CommandInfo repeat = dialect.describe(track, track.commands[1]);
  expect(repeat.detailKind == "capcom-snes.repeat-until", "CapcomSnes repeat opcode should decode as Repeat Until");
  expect(repeat.fields.size() == 3 && repeat.fields[0].value == "1" && repeat.fields[1].value == "2" &&
             repeat.fields[2].value == "$3000",
         "CapcomSnes repeat display should preserve slot, count, and destination");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes finite repeat should render without diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "CapcomSnes repeat count should emit initial defaults and replay the loop body");
  expect(performance.tracks[0].endTick == 18, "CapcomSnes repeat count should include the original pass plus replays");

  for (u64 tick : {0ULL, 6ULL, 12ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
    expect(found, "CapcomSnes repeat fixture should emit a note at tick " + std::to_string(tick));
  }
}

void capcomSnesV1DialectPreservesUnknownOneByteEvents() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x1e;
  bytes[0x3001] = 0xab;
  bytes[0x3002] = 0x1f;
  bytes[0x3003] = 0xcd;
  bytes[0x3004] = 0x41;
  bytes[0x3005] = 0x17;

  const SequenceDialect dialect = capcomSnesSequenceDialect(CapcomSnesEngineVersion::v1BgmInList);
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), dialect, 0, 0x3000);
  expect(track.commands.size() == 4, "CapcomSnes V1 unknown one-byte events should not truncate track decoding");
  expect(dialect.describe(track, track.commands[0]).detailKind == "capcom-snes.unknown-one-byte",
         "CapcomSnes V1 opcode $1E should decode as a one-byte unknown event");
  expect(dialect.describe(track, track.commands[1]).detailKind == "capcom-snes.unknown-one-byte",
         "CapcomSnes V1 opcode $1F should decode as a one-byte unknown event");
  expect(track.commands[0].range.offset == 0x3000 && track.commands[0].range.size == 2,
         "CapcomSnes V1 unknown one-byte event should preserve its source range");

  const auto operands = track.operandsFor(track.commands[0]);
  expect(operands.size() == 2 && operands[0].name == "opcode" && std::get<u64>(operands[0].value) == 0x1e &&
             operands[1].name == "value" && std::get<u64>(operands[1].value) == 0xab,
         "CapcomSnes V1 unknown one-byte event should preserve its opcode and operand");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes V1 unknown one-byte events should render without diagnostics");
  expect(performance.tracks[0].events.size() == 3,
         "CapcomSnes V1 fixture should emit initial defaults and still reach the later note");
}
