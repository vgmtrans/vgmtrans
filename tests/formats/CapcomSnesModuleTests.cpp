/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesModule.h"

#include "value/export/Export.h"
#include "value/export/MidiExporter.h"
#include "value/core/EventSequenceBuilder.h"
#include "value/core/Session.h"
#include "value/formats/CapcomSnes/CapcomSnesProfile.h"
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
  session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesAram());

  const Project project = session.scan();
  expect(project.diagnostics.empty(), "CapcomSnes scan should not report diagnostics for complete fixture");
  expect(project.collections.size() == 1, "CapcomSnes scan should produce one collection");
  expect(project.assets.size() == 3, "CapcomSnes scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first CapcomSnes asset should be sequence");
  expect(sequence->metadata.format == "CapcomSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should point at fixed BGM header body");
  expect(sequence->program.timebase.ppqn == 48, "sequence should use CapcomSnes PPQN");
  expect(sequence->program.behavior.linearAmplitudeScale, "sequence should carry linear amplitude behavior");
  expect(sequence->program.behavior.writeInitialMonoMode, "sequence should carry mono mode behavior");
  expect(sequence->program.behavior.defaultLoopPolicy == LoopPolicy::PlayOnce,
         "sequence should carry CapcomSnes default loop policy");
  expect(sequence->program.sequencerProfile == capcomSnesProfileName(CapcomSnesEngineVersion::v3BgmFixedLocation),
         "sequence should carry the detected CapcomSnes profile key");
  expect(sequence->program.tracks.size() == 8, "sequence should decode all nonzero track pointers");
  expect(std::holds_alternative<TempoCommand>(sequence->program.tracks[0].commands[0]),
         "track should decode tempo command");
  expect(std::get<TempoCommand>(sequence->program.tracks[0].commands[0]).rawValue == 0x1234,
         "tempo command should preserve raw big-endian value");
  expect(std::holds_alternative<ProgramCommand>(sequence->program.tracks[0].commands[1]),
         "track should decode program command");
  expect(std::holds_alternative<VolumeCommand>(sequence->program.tracks[0].commands[2]),
         "track should decode volume command");
  expect(std::holds_alternative<PanCommand>(sequence->program.tracks[0].commands[3]),
         "track should decode pan command");
  expect(std::holds_alternative<LfoCommand>(sequence->program.tracks[0].commands[4]),
         "track should decode LFO command");
  expect(std::holds_alternative<LfoCommand>(sequence->program.tracks[0].commands[5]),
         "track should decode LFO rate command");
  expect(std::holds_alternative<NoteCommand>(sequence->program.tracks[0].commands[6]),
         "track should decode note command");
  expect(std::holds_alternative<EndCommand>(sequence->program.tracks[0].commands[7]),
         "track should decode end command");
  expect(sequence->program.referencedInstruments.size() == 1,
         "sequence should expose unique referenced instruments");
  expect(sequence->program.referencedInstruments[0].bank == 0 &&
             sequence->program.referencedInstruments[0].program == 0,
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
    return item.kind == ItemKind::Command && item.detailKind == "capcom-snes-tempo";
  });
  expect(firstTempoItem != sequenceItems.end(), "sequence item tree should expose typed command nodes");
  expect(firstTempoItem->parent == firstTrackItem->id, "command item should point back to its track item");
  expect(firstTempoItem->name == "Tempo", "command item should carry a readable command name");
  expect(firstTempoItem->description == "Raw 4660", "command item should preserve raw command values");
  expect(firstTempoItem->range.offset == 0x3000 && firstTempoItem->range.size == 3,
         "command item should preserve command source range");

  const EventSequence eventSequence = EventSequenceBuilder().build(
      sequence->program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce);
  expect(eventSequence.diagnostics.empty(), "CapcomSnes event sequence build should not warn for linear fixture");
  expect(eventSequence.tracks.size() == 8, "builder should preserve track count");
  expect(eventSequence.tracks[0].events.size() == 14, "built track should include initial, command, and end events");
  expect(std::holds_alternative<MonoMode>(eventSequence.tracks[0].events[0]),
         "builder should emit initial mono mode from sequence behavior");
  expect(std::get<MonoMode>(eventSequence.tracks[0].events[0]).channels == 0,
         "initial mono mode should match legacy MIDI controller payload");
  expect(std::holds_alternative<Reverb>(eventSequence.tracks[0].events[1]),
         "builder should emit initial reverb from sequence behavior");
  expect(std::get<Tempo>(eventSequence.tracks[0].events[2]).microsecondsPerQuarter == 42191,
         "CapcomSnes profile should interpret tempo with legacy timing math");
  expect(std::holds_alternative<BankSelect>(eventSequence.tracks[0].events[3]),
         "CapcomSnes event sequence build should include bank select before program changes");
  expect(!std::get<BankSelect>(eventSequence.tracks[0].events[3]).writeLsb,
         "CapcomSnes bank select should match legacy MSB-only output");
  expect(std::holds_alternative<ProgramChange>(eventSequence.tracks[0].events[4]),
         "CapcomSnes event sequence build should include program changes");
  expect(std::holds_alternative<Volume14>(eventSequence.tracks[0].events[5]),
         "CapcomSnes V3 profile should interpret volume to 14-bit volume");
  expect(std::get<Pan>(eventSequence.tracks[0].events[6]).value == 64,
         "CapcomSnes center pan should map to MIDI center pan");
  expect(std::holds_alternative<Expression>(eventSequence.tracks[0].events[7]),
         "CapcomSnes pan emission should include expression compensation");
  expect(std::get<VibratoDepth>(eventSequence.tracks[0].events[8]).value == 0,
         "CapcomSnes LFO type 0 should store vibrato depth but emit zero while rate is disabled");
  expect(std::get<VibratoDepth>(eventSequence.tracks[0].events[9]).value == 0x20,
         "CapcomSnes LFO rate should emit stored vibrato depth when output becomes enabled");
  expect(std::holds_alternative<VibratoFrequency>(eventSequence.tracks[0].events[10]),
         "CapcomSnes LFO rate should map to vibrato frequency");
  expect(std::holds_alternative<TremoloFrequency>(eventSequence.tracks[0].events[11]),
         "CapcomSnes LFO rate should map to tremolo frequency");
  expect(std::get<NoteDuration>(eventSequence.tracks[0].events[12]).duration == 6,
         "CapcomSnes note length index should map to ticks");
  expect(std::get<EndOfTrack>(eventSequence.tracks[0].events[13]).tick == 6,
         "builder should advance time before end of track");

  const auto artifacts = session.exportCollection(project.collections[0].id, ExportRequest{
                                                                                 .kinds = {ExportKind::Midi},
                                                                                 .loopPolicy = LoopPolicy::PlayOnce,
                                                                             });
  expect(artifacts.size() == 1, "value export should produce one MIDI artifact");
  expect(artifacts[0].filename == "Mega Man X.mid", "MIDI artifact should use collection name");
  expect(artifacts[0].mediaType == "audio/midi", "MIDI artifact should use audio/midi media type");
  expect(artifacts[0].diagnostics.empty(), "MIDI artifact should not carry diagnostics for linear fixture");
  expect(artifacts[0].bytes == MidiExporter().exportMidi(eventSequence),
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
  expect(instrument.generators.size() == 2, "instrument should carry legacy LFO generator settings");
  expect(instrument.generators[0].destination == SynthDestination::VibratoRate &&
             instrument.generators[0].amount == -8479,
         "instrument should carry legacy vibrato base frequency");
  expect(instrument.generators[1].destination == SynthDestination::TremoloRate &&
             instrument.generators[1].amount == -7279,
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

  const Project project = session.scan();
  expect(project.diagnostics.empty(), "SPC-backed CapcomSnes scan should not report diagnostics");
  expect(project.sources.size() == 2, "SPC scan should preserve original source plus extracted ARAM");
  expect(!project.sources[0].virtualized, "original SPC source should not be virtualized");
  expect(project.sources[1].virtualized, "SPC RAM source should be virtualized");
  expect(project.sources[1].name == "Mega Man X.spc - ram", "virtual ARAM source should match legacy naming");
  expect(project.sources[1].title == "Capcom Logo", "virtual ARAM source should carry the SPC title tag");
  expect(project.sources[1].origin.has_value(), "virtual ARAM source should preserve origin range");
  expect(project.sources[1].origin->source == sourceId, "virtual ARAM origin should point at the SPC source");
  expect(project.sources[1].origin->offset == 0x100 && project.sources[1].origin->size == 0x10000,
         "virtual ARAM origin should point at SPC RAM bytes");

  expect(project.collections.size() == 1, "SPC-backed scan should produce one collection");
  expect(project.collections[0].name == "Capcom Logo", "SPC-backed collection should use the SPC title tag");
  expect(project.assets.size() == 3, "SPC-backed scan should produce CapcomSnes assets from virtual ARAM");
  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "SPC-backed scan should produce a sequence");
  expect(sequence->metadata.name == "Capcom Logo", "SPC-backed sequence should use the SPC title tag");
  expect(sequence->metadata.range.source == SourceId{1}, "sequence range should point at virtual ARAM source");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should preserve ARAM-relative address");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets[2]);
  expect(samples != nullptr, "SPC-backed scan should produce samples");
  expect(!samples->samples.samples.empty(), "SPC-backed scan should discover sample data");
  expect(samples->samples.samples[0].encodedData.source == SourceId{1},
         "sample encoded data should point at virtual ARAM source");
}

void capcomSnesNoteStateCommandsAreTypedAndLowered() {
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

  const Project project = session.scan();
  expect(project.diagnostics.empty(), "CapcomSnes note-state scan should not report diagnostics");
  expect(!project.assets.empty(), "CapcomSnes note-state scan should produce assets");

  const auto* sequence = std::get_if<SequenceAsset>(&project.assets[0]);
  expect(sequence != nullptr, "CapcomSnes note-state scan should produce a sequence");
  expect(!sequence->program.tracks.empty(), "CapcomSnes note-state scan should decode tracks");

  const auto& commands = sequence->program.tracks[0].commands;
  expect(commands.size() == 4, "CapcomSnes note-state fixture should decode four commands");

  const auto* octave = std::get_if<NoteStateCommand>(&commands[0]);
  expect(octave != nullptr, "CapcomSnes octave opcode should decode as a typed note-state command");
  expect(octave->action == NoteStateAction::Octave && octave->rawValue == 4,
         "CapcomSnes octave command should preserve its raw octave operand");
  expect(octave->range.offset == 0x3000 && octave->range.size == 2,
         "CapcomSnes octave command should preserve its source range");

  const auto* attributes = std::get_if<NoteStateCommand>(&commands[1]);
  expect(attributes != nullptr, "CapcomSnes attributes opcode should decode as a typed note-state command");
  expect(attributes->action == NoteStateAction::Attributes && attributes->rawValue == 0x48,
         "CapcomSnes note attributes should preserve their raw attribute byte");
  expect(attributes->range.offset == 0x3002 && attributes->range.size == 2,
         "CapcomSnes note attributes should preserve their source range");

  const auto attributeItem = std::ranges::find_if(sequence->metadata.items.nodes, [](const ItemNode& item) {
    return item.kind == ItemKind::Command && item.detailKind == "capcom-snes-note-attributes" &&
           item.range.offset == 0x3002;
  });
  expect(attributeItem != sequence->metadata.items.nodes.end(),
         "CapcomSnes item tree should expose typed note-attribute command nodes");
  expect(attributeItem->name == "Note Attributes", "note-attribute item should carry a readable name");
  expect(attributeItem->description == "Raw 72", "note-attribute item should preserve raw command values");

  const EventSequence eventSequence = EventSequenceBuilder().build(
      sequence->program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce);
  expect(eventSequence.diagnostics.empty(), "CapcomSnes note-state emission should not report diagnostics");
  expect(!eventSequence.tracks.empty(), "CapcomSnes note-state emission should preserve tracks");

  const auto& events = eventSequence.tracks[0].events;
  const auto legato = std::ranges::find_if(events, [](const Event& event) {
    const auto* typed = std::get_if<LegatoPedal>(&event);
    return typed != nullptr && typed->tick == 0 && typed->enabled;
  });
  expect(legato != events.end(), "CapcomSnes note attributes should interpret slur state to legato pedal");

  const auto note = std::ranges::find_if(events, [](const Event& event) {
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

void capcomSnesPortamentoUsesSourceKeyDistanceUnderTranspose() {
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0x3000},
          .commands = {
              PortamentoCommand{.rawTime = 0x40},
              NoteCommand{.key = 5, .rawDuration = 7},
              TransposeCommand{.rawSemitones = 1},
              NoteCommand{.key = 8, .rawDuration = 7},
              EndCommand{},
          },
      }},
      .behavior = SequenceBehavior{.initialGlobalTranspose = 6},
  };

  const EventSequence eventSequence = EventSequenceBuilder().build(
      program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce);
  const auto& events = eventSequence.tracks[0].events;

  const auto portamentoTime = std::ranges::find_if(events, [](const Event& event) {
    const auto* time = std::get_if<PortamentoTime14>(&event);
    return time != nullptr && time->tick == 192;
  });
  expect(portamentoTime != events.end(), "CapcomSnes portamento should emit 14-bit time before the next note");
  expect(std::get<PortamentoTime14>(*portamentoTime).value == 96,
         "CapcomSnes portamento distance should use source keys, ignoring active transpose");

  const auto portamentoControl = std::ranges::find_if(events, [](const Event& event) {
    const auto* control = std::get_if<PortamentoControl>(&event);
    return control != nullptr && control->tick == 192;
  });
  expect(portamentoControl != events.end(), "CapcomSnes portamento should emit previous-key control");
  expect(std::get<PortamentoControl>(*portamentoControl).key == 10,
         "CapcomSnes portamento control should include global but not local transpose");

  const auto secondNote = std::ranges::find_if(events, [](const Event& event) {
    const auto* note = std::get_if<NoteDuration>(&event);
    return note != nullptr && note->tick == 192;
  });
  expect(secondNote != events.end(), "CapcomSnes portamento fixture should emit the second note");
  expect(std::get<NoteDuration>(*secondNote).key == 14,
         "CapcomSnes note pitch should still include active global and local transpose");
}

void capcomSnesPanEventsDoNotRecurveMidiPan() {
  const CommandSequence v3Program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0x3000},
          .commands = {
              PanCommand{.rawValue = 0x40},
              EndCommand{},
          },
      }},
  };

  const EventSequence eventSequence = EventSequenceBuilder().build(
      v3Program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce);
  expect(eventSequence.diagnostics.empty(), "CapcomSnes pan fixture should build without diagnostics");
  expect(!eventSequence.tracks.empty(), "CapcomSnes pan fixture should emit one track");

  const auto& events = eventSequence.tracks[0].events;
  const auto pan = std::ranges::find_if(events, [](const Event& event) {
    const auto* typed = std::get_if<Pan>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(pan != events.end(), "CapcomSnes pan fixture should emit a pan controller");
  expect(std::get<Pan>(*pan).value == 113,
         "CapcomSnes pan emission should emit the computed MIDI pan without applying the linear pan curve again");

  const auto expression = std::ranges::find_if(events, [](const Event& event) {
    const auto* typed = std::get_if<Expression>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(expression != events.end(), "CapcomSnes pan fixture should emit expression compensation");
  expect(std::get<Expression>(*expression).value == 123,
         "CapcomSnes pan compensation should quantize after the amplitude curve");

  const CommandSequence v1Program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0x3000},
          .commands = {
              PanCommand{.rawValue = 0x01},
              EndCommand{},
          },
      }},
  };

  const EventSequence v1EventSequence = EventSequenceBuilder().build(
      v1Program, CapcomSnesProfile(CapcomSnesEngineVersion::v1BgmInList), LoopPolicy::PlayOnce);
  expect(v1EventSequence.diagnostics.empty(), "CapcomSnes V1 pan fixture should build without diagnostics");
  const auto& v1Events = v1EventSequence.tracks[0].events;
  const auto v1Pan = std::ranges::find_if(v1Events, [](const Event& event) {
    const auto* typed = std::get_if<Pan>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(v1Pan != v1Events.end(), "CapcomSnes V1 pan fixture should emit a pan controller");
  expect(std::get<Pan>(*v1Pan).value == 65,
         "CapcomSnes V1 pan emission should apply the pan curve before reducing to a MIDI controller value");
}

void capcomSnesV1VolumeQuantizesAfterAmplitudeCurve() {
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0x3000},
          .commands = {
              VolumeCommand{.rawValue = 0x01},
              MasterVolumeCommand{.rawValue = 0x03},
              EndCommand{},
          },
      }},
  };

  const EventSequence eventSequence = EventSequenceBuilder().build(
      program, CapcomSnesProfile(CapcomSnesEngineVersion::v1BgmInList), LoopPolicy::PlayOnce);
  expect(eventSequence.diagnostics.empty(), "CapcomSnes V1 volume fixture should build without diagnostics");
  expect(!eventSequence.tracks.empty(), "CapcomSnes V1 volume fixture should emit one track");

  const auto& events = eventSequence.tracks[0].events;
  const auto volume = std::ranges::find_if(events, [](const Event& event) {
    const auto* typed = std::get_if<Volume14>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(volume != events.end(), "CapcomSnes V1 volume should map to a 14-bit MIDI volume controller");
  expect(std::get<Volume14>(*volume).value == 1026,
         "CapcomSnes V1 volume should apply the amplitude curve before MIDI quantization");

  const auto masterVolume = std::ranges::find_if(events, [](const Event& event) {
    const auto* typed = std::get_if<MasterVolume>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(masterVolume != events.end(), "CapcomSnes V1 master volume should map to MIDI master volume");
  expect(std::get<MasterVolume>(*masterVolume).value == 1777,
         "CapcomSnes V1 master volume should apply the amplitude curve before MIDI quantization");
}

void capcomSnesMidiExportUsesSequenceProfileKey() {
  const CommandSequence program{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {CommandTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 0,
          .startAddress = Address{0x3000},
          .commands = {
              PanCommand{.rawValue = 0x01},
              EndCommand{},
          },
      }},
      .sequencerProfile = std::string(capcomSnesProfileName(CapcomSnesEngineVersion::v1BgmInList)),
  };

  Project project;
  project.assets.emplace_back(SequenceAsset{
      .metadata = AssetMetadata{
          .id = AssetId{0},
          .format = "CapcomSnes",
          .name = "V1",
      },
      .program = program,
  });
  project.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "V1",
      .sequence = AssetId{0},
  });

  SourceStore sources;
  SequencerProfileRegistry profiles;
  registerCapcomSnesProfile(profiles);

  const auto artifacts = ExportService().exportCollection(project,
                                                          sources,
                                                          CollectionId{0},
                                                          ExportRequest{
                                                              .kinds = {ExportKind::Midi},
                                                              .loopPolicy = LoopPolicy::PlayOnce,
                                                          },
                                                          profiles);
  expect(artifacts.size() == 1, "CapcomSnes profile-key export should produce one MIDI artifact");
  expect(artifacts[0].diagnostics.empty(), "CapcomSnes profile-key export should not report diagnostics");

  const auto v1Bytes = MidiExporter().exportMidi(EventSequenceBuilder().build(
      program, CapcomSnesProfile(CapcomSnesEngineVersion::v1BgmInList), LoopPolicy::PlayOnce));
  const auto v3Bytes = MidiExporter().exportMidi(EventSequenceBuilder().build(
      program, CapcomSnesProfile(CapcomSnesEngineVersion::v3BgmFixedLocation), LoopPolicy::PlayOnce));
  expect(artifacts[0].bytes == v1Bytes, "MIDI export should use the sequence's explicit CapcomSnes profile key");
  expect(artifacts[0].bytes != v3Bytes, "MIDI export should not fall back to the default CapcomSnes profile");
}
