/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SynthMath.h"

#include "ValueFormatTestSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::konami_snes;

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

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view localKind) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.localKind == localKind) {
      return &annotation;
    }
  }
  return nullptr;
}

template <class Event>
bool hasMidiEvent(const MidiTrack& track) {
  return std::ranges::any_of(track.events, [](const MidiEvent& event) { return std::holds_alternative<Event>(event); });
}

bool hasNonZeroPitchBendBefore(const MidiTrack& track, u64 tick) {
  return std::ranges::any_of(track.events, [tick](const MidiEvent& event) {
    const auto* pitchBend = std::get_if<PitchBend>(&event);
    return pitchBend != nullptr && pitchBend->tick < tick && pitchBend->value != 0;
  });
}

bool hasNonZeroPitchBendAtOrAfter(const MidiTrack& track, u64 tick) {
  return std::ranges::any_of(track.events, [tick](const MidiEvent& event) {
    const auto* pitchBend = std::get_if<PitchBend>(&event);
    return pitchBend != nullptr && pitchBend->tick >= tick && pitchBend->value != 0;
  });
}

std::vector<u8> makeKonamiSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 10> setSongHeaderAddressGG4{0x8f, 0x00, 0x0a, 0x8f, 0x20, 0x0b, 0xcd, 0x00, 0xd8, 0x1c};
  writeBytes(bytes, 0x0100, setSongHeaderAddressGG4);

  constexpr std::array<u8, 15> jumpToVcmdGG4{0x1c, 0xfd, 0xf6, 0xbc, 0x1a, 0x2d, 0xf6, 0xbb,
                                             0x1a, 0x2d, 0xf6, 0x00, 0x03, 0xf0, 0x08};
  writeBytes(bytes, 0x0120, jumpToVcmdGG4);
  writeLe16(bytes, 0x0120 + 11, 0x0300);

  constexpr std::array<u8, 6> setDirGG4{0x8f, 0x5d, 0xf2, 0x8f, 0x50, 0xf3};
  writeBytes(bytes, 0x0140, setDirGG4);

  constexpr std::array<u8, 48> loadInstrGG4{0x09, 0x11, 0x10, 0xfd, 0xf5, 0xa1, 0x01, 0xd0, 0x27, 0xdd, 0x68, 0x28,
                                            0xb0, 0x0c, 0x8f, 0x3c, 0x04, 0x8f, 0x0a, 0x05, 0x3f, 0xee, 0x1b, 0x5f,
                                            0xe2, 0x18, 0xa8, 0x28, 0x2d, 0xeb, 0x25, 0xf6, 0x20, 0x0a, 0xc4, 0x04,
                                            0xf6, 0x21, 0x0a, 0xc4, 0x05, 0xae, 0x3f, 0xee, 0x1b, 0x5f, 0xe2, 0x18};
  writeBytes(bytes, 0x0160, loadInstrGG4);
  bytes[0x0160 + 11] = 0x01;
  bytes[0x0160 + 15] = 0x00;
  bytes[0x0160 + 18] = 0x40;
  bytes[0x0160 + 30] = 0x10;
  writeLe16(bytes, 0x0160 + 32, 0x4100);

  constexpr std::array<u8, 13> loadPercInstrGG4{0x8f, 0x00, 0x04, 0x8f, 0x43, 0x05, 0x8d,
                                                0x07, 0xcf, 0x7a, 0x04, 0xda, 0x04};
  writeBytes(bytes, 0x01c0, loadPercInstrGG4);

  bytes[0x0010] = 0x00;
  writeLe16(bytes, 0x4100, 0x4200);
  bytes[0x4200] = 0xff;
  bytes[0x4305] = 0x29;

  writeLe16(bytes, 0x2000, 0x2002);
  bytes[0x2002] = 0xea;
  bytes[0x2003] = 0x80;
  bytes[0x2004] = 0xe2;
  bytes[0x2005] = 0x00;
  bytes[0x2006] = 0xee;
  bytes[0x2007] = 0x7f;
  bytes[0x2008] = 0xe3;
  bytes[0x2009] = 0x14;
  bytes[0x200a] = 0xe4;
  bytes[0x200b] = 0x08;
  bytes[0x200c] = 0x20;
  bytes[0x200d] = 0x10;
  bytes[0x200e] = 0x3c;
  bytes[0x200f] = 0x06;
  bytes[0x2010] = 0x7f;
  bytes[0x2011] = 0x7f;
  bytes[0x2012] = 0xff;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x00;
  bytes[0x4002] = 0x00;
  bytes[0x4003] = 0x8f;
  bytes[0x4004] = 0xe0;
  bytes[0x4005] = 0x14;
  bytes[0x4006] = 0x7f;

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

void writeKonamiInstrumentEntry(std::vector<u8>& bytes, u32 offset, u8 srcn) {
  constexpr std::array<u8, 6> fields{
      0x00,  // key
      0x00,  // tuning
      0x8f,  // ADSR1
      0xe0,  // ADSR2
      0x14,  // pan
      0x20,  // volume
  };
  bytes[offset] = srcn;
  writeBytes(bytes, offset + 1, fields);
}

std::vector<u8> makeKonamiSnesBuilderAram() {
  std::vector<u8> bytes(0x10000);

  // Programs 1-3 are deliberately empty. Program 4 therefore proves that
  // sparse source programs do not leak into dense annotation ownership.
  writeKonamiInstrumentEntry(bytes, 0x4000, 3);
  bytes[0x4007] = 0xff;
  bytes[0x400e] = 0xff;
  bytes[0x4015] = 0xff;
  writeKonamiInstrumentEntry(bytes, 0x401c, 2);
  writeKonamiInstrumentEntry(bytes, 0x4200, 4);
  bytes[0x4207] = 0xff;

  // Three source entries intentionally join one percussion instrument.
  writeKonamiInstrumentEntry(bytes, 0x4300, 1);
  writeKonamiInstrumentEntry(bytes, 0x4307, 4);
  writeKonamiInstrumentEntry(bytes, 0x430e, 5);
  bytes[0x4315] = 0xff;
  bytes[0x431a] = 0xff;

  const auto directoryEntry = [&](u8 srcn, u16 start) {
    const u32 offset = 0x5000 + static_cast<u32>(srcn) * 4;
    writeLe16(bytes, offset, start);
    writeLe16(bytes, offset + 2, start);
    bytes[start] = 0x01;
  };
  directoryEntry(1, 0x5100);
  directoryEntry(2, 0x5100);  // Explicit alias of SRCN 1.
  directoryEntry(3, 0xa100);  // 0xa100 - DIR base 0x5000 resolves to 0x5100.
  directoryEntry(4, 0x6200);
  directoryEntry(5, 0x3000);  // Below the DIR base, so Konami falls back to sample zero.
  return bytes;
}

PerformanceSequence renderKonamiSnesTrack(std::span<const u8> commandBytes) {
  std::vector<u8> bytes(commandBytes.begin(), commandBytes.end());
  const auto& dialect = konamiSnesSequenceDialect(KONAMISNES_V6);
  TrackProgram track = decodeKonamiSnesSourceTrack(ByteReader(SourceId{9}, bytes), KONAMISNES_V6, 0, 0);
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{0},
      .config = SequenceProgramConfig{.profile = KONAMISNES_V6},
      .behavior = dialect.defaultBehavior,
      .tracks = {track},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
}

PerformanceSequence renderKonamiSnesProgram(KonamiSnesVersion version, const std::vector<std::vector<u8>>& tracks,
                                            u32 sequenceLoops = 0) {
  const auto& dialect = konamiSnesSequenceDialect(version);
  std::vector<TrackProgram> programTracks;
  programTracks.reserve(tracks.size());
  for (u32 trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
    programTracks.push_back(decodeKonamiSnesSourceTrack(ByteReader(SourceId{100 + trackIndex}, tracks[trackIndex]),
                                                        version, trackIndex, 0));
  }
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{0},
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .behavior = dialect.defaultBehavior,
      .tracks = std::move(programTracks),
  };
  return SequenceVm(SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = sequenceLoops})
      .render(program, dialect);
}

}  // namespace

void konamiSnesLayoutDiscoversDirectHeaderAndSynthTables() {
  const auto bytes = makeKonamiSnesAram();
  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should match the value scanner layout patterns");
  expect(layout->version == KONAMISNES_V6, "direct GG4-style fixture should be classified as KonamiSnes V6");
  expect(layout->sequenceHeaderAddress == 0x2000, "layout should recover the direct sequence header address");
  expect(layout->spcDirAddress == 0x5000, "layout should recover the SPC DIR address");
  expect(layout->commonInstrumentTableAddress == 0x4000, "layout should recover the common instrument table");
  expect(layout->bankedInstrumentTableAddress == 0x4200, "layout should resolve the active banked instrument table");
  expect(layout->percussionInstrumentTableAddress == 0x4300, "layout should recover the percussion table");
}

void konamiSnesLayoutInfersSpcDirFromInstrumentTables() {
  auto bytes = makeKonamiSnesAram();
  std::fill(bytes.begin() + 0x0140, bytes.begin() + 0x0146, u8{0});

  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should still match without a DIR write pattern");
  expect(layout->spcDirAddress == 0x5000, "layout should infer SPC DIR from valid instrument sample references");
}

void konamiSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  expect(session.dialects().contains("konami-snes:v6"),
         "value format registration should include KonamiSnes sequence dialects");

  const SourceId source = session.addSource(SourceFile{.name = "Axelay.spc"}, makeKonamiSnesAram());
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();
  expect(project.diagnostics().empty(), "KonamiSnes scan should not report diagnostics for the complete fixture");
  expect(project.collections().size() == 1, "KonamiSnes scan should produce one collection");
  expect(project.assets().size() == 3, "KonamiSnes scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "first KonamiSnes asset should be a sequence");
  expect(sequence->metadata.format == "KonamiSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2000 && sequence->metadata.range.size == 2,
         "sequence range should cover the inferred one-track header");
  expect(sequence->program.dialect.value == "konami-snes:v6", "sequence should carry the detected dialect");
  expect(sequence->program.timebase.ppqn == 48, "KonamiSnes sequence should use the SNES value PPQN");
  expect(sequence->program.tracks.size() == 1, "fixture should decode one nonzero source track");

  const TrackProgram& track = sequence->program.tracks.front();
  expect(track.commands.size() == 7, "KonamiSnes fixture should decode tempo, setup, note, and end commands");
  constexpr std::array<std::string_view, 7> expectedCommandDetailKinds{
      "konami-snes.tempo",   "konami-snes.program", "konami-snes.volume", "konami-snes.pan",
      "konami-snes.vibrato", "konami-snes.note",    "konami-snes.end"};
  for (size_t index = 0; index < expectedCommandDetailKinds.size(); ++index) {
    expect(commandDetailKind(project.sourceMap(), track.commands[index]) == expectedCommandDetailKinds[index],
           "track should decode KonamiSnes command " + std::to_string(index));
  }

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "registered KonamiSnes dialect should render the scanned sequence");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  expect(performance.diagnostics.empty(), "KonamiSnes performance render should not report diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 6,
         "KonamiSnes note duration should use the decoded duration rate");

  const auto vibratoDepth = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
           modulation->amount > 0.0;
  });
  expect(vibratoDepth != performance.tracks[0].events.end(),
         "KonamiSnes vibrato command should emit target-neutral depth");
  const auto& vibratoDepthEvent = std::get<ModulationPerformanceEvent>(*vibratoDepth);
  const double expectedDepthCents = vibrato::currentDepthCents(KONAMISNES_V6, 0x10, 0x10 << 8);
  const double expectedDepthAmount =
      expectedDepthCents / vibrato::maxDepthCents(KONAMISNES_V6, kDefaultVibratoMaxDepth);
  expect(std::abs(vibratoDepthEvent.amount - expectedDepthAmount) < 0.0001,
         "KonamiSnes vibrato depth should be normalized against the full synth range");
  expect(vibratoDepthEvent.pitchDepthSemitones &&
             std::abs(*vibratoDepthEvent.pitchDepthSemitones - (expectedDepthCents / 100.0)) < 0.0001,
         "KonamiSnes vibrato depth should retain peak pitch swing for sequence-event simulation");
  const auto vibratoDelay = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<VibratoDelayPerformanceEvent>(event);
  });
  expect(vibratoDelay != performance.tracks[0].events.end(),
         "KonamiSnes vibrato command should emit target-neutral delay");
  expect(std::get<VibratoDelayPerformanceEvent>(*vibratoDelay).delayTicks == 2,
         "KonamiSnes vibrato delay should be converted to rendered sequence ticks");

  const MidiSequence synthModulationMidi = renderMidiSequence(performance);
  expect(hasMidiEvent<VibratoDepth>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoFrequency>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoDelay>(synthModulationMidi.tracks[0]),
         "default KonamiSnes MIDI rendering should preserve synth modulation controllers");

  const MidiSequence simulatedMidi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  expect(!hasMidiEvent<VibratoDepth>(simulatedMidi.tracks[0]) &&
             !hasMidiEvent<VibratoFrequency>(simulatedMidi.tracks[0]) &&
             !hasMidiEvent<VibratoDelay>(simulatedMidi.tracks[0]),
         "sequence-event modulation policy should suppress synth modulation controllers");
  expect(!hasNonZeroPitchBendBefore(simulatedMidi.tracks[0], 2),
         "sequence-event modulation policy should keep simulated vibrato silent before delay");
  expect(hasNonZeroPitchBendAtOrAfter(simulatedMidi.tracks[0], 2),
         "sequence-event modulation policy should emit nonzero vibrato pitch bend after delay");

  const auto* instruments = std::get_if<InstrumentSetAsset>(&project.assets()[1]);
  expect(instruments != nullptr, "second KonamiSnes asset should be an instrument set");
  expect(instruments->instruments.size() == 1, "instrument set should parse one valid melodic instrument");
  const Instrument& instrument = instruments->instruments.front();
  expect(instrument.explicitAddress == InstrumentAddress{.bank = 0, .program = 0},
         "instrument should preserve its explicit export address");
  expect(instrument.range.offset == 0x4000 && instrument.range.size == 7,
         "instrument should preserve its source header range");
  expect(instrument.regions.size() == 1, "instrument should contain one sample-backed region");
  expect(instrument.modulation.vibrato.has_value(), "KonamiSnes instruments should describe vibrato");
  expect(instrument.modulation.vibrato->maxDepthCents > 0.0,
         "KonamiSnes instruments should describe a positive vibrato depth range");
  expect(instrument.modulation.vibrato->delaySeconds.has_value(),
         "KonamiSnes instruments should describe the vibrato delay range");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets()[2]);
  expect(samples != nullptr, "third KonamiSnes asset should be a sample collection");
  expect(samples->samples.samples.size() == 1, "sample collection should parse one referenced BRR sample");
  expect(samples->samples.samples.front().encodedData.offset == 0x6000 &&
             samples->samples.samples.front().encodedData.size == 9,
         "sample should preserve the one-block BRR payload range");
  expect(!samples->samples.samples.front().loop.enabled && samples->samples.samples.front().loop.start == 0 &&
             samples->samples.samples.front().loop.length == 0,
         "non-looping KonamiSnes BRR samples should keep a zero loop span");

  const SourceMap& sourceMap = project.sourceMap();
  const auto* sequenceHeader = annotationWithKind(sourceMap, source, SourceRole::Header, "konami-snes-sequence-header");
  expect(sequenceHeader != nullptr && sequenceHeader->range.offset == 0x2000 && sequenceHeader->range.size == 2,
         "KonamiSnes scan should annotate the sequence header");
  const auto* trackPointer = annotationWithKind(sourceMap, source, SourceRole::Pointer, "konami-snes-track-pointer");
  expect(trackPointer != nullptr && trackPointer->range.offset == 0x2000 && trackPointer->range.size == 2,
         "KonamiSnes scan should annotate track pointers");
  const auto* instrumentTable =
      annotationWithKind(sourceMap, source, SourceRole::Table, "konami-snes-instrument-tables");
  expect(instrumentTable != nullptr && instrumentTable->range.offset == 0x4000 && instrumentTable->range.size == 7,
         "KonamiSnes scan should annotate instrument tables");
  const auto* instrumentRow = annotationWithKind(sourceMap, source, SourceRole::Instrument, "konami-snes-instrument");
  expect(instrumentRow != nullptr && instrumentRow->range.offset == 0x4000 && instrumentRow->range.size == 7,
         "KonamiSnes scan should annotate parsed instrument rows");
  const auto instrumentSampleLink = std::ranges::find_if(
      instrumentRow->links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
  expect(instrumentSampleLink != instrumentRow->links.end(),
         "KonamiSnes instrument annotation should link to the referenced sample");
  const auto* regionAnnotation = annotationWithKind(sourceMap, source, SourceRole::Region, "konami-snes-region");
  expect(regionAnnotation != nullptr && regionAnnotation->owner == ObjectRefs::region(instruments->metadata.id, 0, 0),
         "KonamiSnes region annotations should identify their durable instrument and region indexes");
  const auto* sampleDir = annotationWithKind(sourceMap, source, SourceRole::Table, "snes-sample-dir");
  expect(sampleDir != nullptr && sampleDir->range.offset == 0x5000 && sampleDir->range.size == 4,
         "KonamiSnes scan should annotate the sample DIR table");
  const auto* sampleEntry = annotationWithKind(sourceMap, source, SourceRole::Sample, "snes-sample-dir-entry");
  expect(sampleEntry != nullptr && sampleEntry->range.offset == 0x5000 && sampleEntry->range.size == 4,
         "KonamiSnes scan should annotate sample DIR entries");
  const auto* samplePayload = annotationWithKind(sourceMap, source, SourceRole::Payload, "snes-brr-payload");
  expect(samplePayload != nullptr && samplePayload->range.offset == 0x6000 && samplePayload->range.size == 9,
         "KonamiSnes scan should annotate BRR payloads");
}

void konamiSnesSynthParsersStopAtInvalidBankedInstrument() {
  const auto bytes = makeKonamiSnesAram();
  const auto layout = findKonamiSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "KonamiSnes fixture should expose a layout for synth parser tests");
  const auto instruments = parseKonamiSnesInstrumentInfos(ByteReader(SourceId{8}, bytes), *layout);
  expect(instruments.size() == 1, "KonamiSnes parser should stop at the first invalid banked instrument");
  expect(instruments.front().index == 0 && instruments.front().source.range.offset == 0x4000,
         "KonamiSnes parser should preserve the sparse source instrument index and address");
  const auto samples = parseKonamiSnesSampleInfos(ByteReader(SourceId{8}, bytes), *layout->spcDirAddress, instruments);
  expect(samples.samples.size() == 1 && samples.samples.front().srcn == 0 &&
             samples.samples.front().stream.encodedData.size == 9,
         "KonamiSnes sample parser should keep only samples used by valid instruments");
}

void konamiSnesSynthBuilderGroupsPercussionAndPreservesSampleRules() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "konami-builder.spc"}, makeKonamiSnesBuilderAram());
  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  };
  ScanResultBuilder result(input, "KonamiSnes");
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto sampleCollection = result.reserveSampleCollection();
  const KonamiSnesLayout layout{
      .version = KONAMISNES_V6,
      .spcDirAddress = 0x5000,
      .commonInstrumentTableAddress = 0x4000,
      .bankedInstrumentTableAddress = 0x4200,
      .firstBankedInstrument = 5,
      .percussionInstrumentTableAddress = 0x4300,
  };
  expect(addKonamiSnesSynth(result, instrumentSet, sampleCollection, layout, "Builder Probe"),
         "KonamiSnes builder fixture should produce a complete synth");
  const ScanResult scan = result.finish();

  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  const auto* samples = std::get_if<SampleCollectionAsset>(&scan.assets[1]);
  expect(instruments != nullptr && samples != nullptr && instruments->instruments.size() == 4 &&
             samples->samples.samples.size() == 5,
         "KonamiSnes builder should retain three melodic programs, one grouped kit, and every source sample");
  expect(instruments->instruments[0].regions[0].sample.index == 0,
         "KonamiSnes transformed-address lookup should resolve SRCN 3 to the BRR stream at relative address 0x5100");
  expect(instruments->instruments[1].regions[0].sample.index == 0,
         "two SRCNs that name one BRR stream should resolve to the same canonical sample");
  expect(instruments->instruments[2].regions[0].sample.index == 3,
         "ordinary Konami sample lookup should retain the SRCN's concrete sample reference");

  const Instrument& percussion = instruments->instruments[3];
  expect(percussion.explicitAddress == InstrumentAddress{.bank = 127, .program = 0} && percussion.regions.size() == 3,
         "percussion source entries should form one drum kit through getOrAdd");
  expect(percussion.regions[0].sample.index == 0 && percussion.regions[1].sample.index == 3 &&
             percussion.regions[2].sample.index == 0,
         "percussion should preserve direct, distinct, and legacy sample-zero fallback choices");

  const auto sparseSources = scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentSet.id, 1));
  expect(sparseSources.size() == 1 && scan.sourceMap.get(sparseSources[0]).range.offset == 0x401c,
         "sparse source program 4 should use dense instrument owner 1");
  expect(scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentSet.id, 4)).empty(),
         "a sparse source program must not be mistaken for a dense annotation owner");

  const auto percussionSources = scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentSet.id, 3));
  expect(percussionSources.size() == 3, "every percussion source entry should point back to the one durable drum kit");
  for (const SourceAnnotationId id : percussionSources) {
    const SourceAnnotation& annotation = scan.sourceMap.get(id);
    const auto sampleLinks = std::ranges::count_if(
        annotation.links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
    expect(sampleLinks == 2,
           "each drum-kit source record should expose the kit's complete, deduplicated sample relationship");
  }
  for (u32 regionIndex = 0; regionIndex < percussion.regions.size(); ++regionIndex) {
    expect(scan.sourceMap.ownedBy(ObjectRefs::region(instrumentSet.id, 3, regionIndex)).size() == 1,
           "each grouped percussion region should retain its own stable source owner");
  }
}

void konamiSnesProgramChangeReemitsCurrentFineTune() {
  constexpr std::array<u8, 15> bytes{
      0xf2, 0xf4,              // tune down
      0x3c, 0x05, 0x7f, 0x7f,  // establish the active fine tune before the switch
      0xe2, 0x09,              // switch to program 9 while tuning is still active
      0x3c, 0x05, 0x7f, 0x7f,  // note at the same tick
      0xff,
  };

  const PerformanceSequence performance = renderKonamiSnesTrack(bytes);
  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;

  const auto programChange = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* program = std::get_if<ProgramChange>(&event);
    return program != nullptr && program->tick == 5 && program->program == 9;
  });
  expect(programChange != events.end(), "KonamiSnes program change should render at the expected tick");

  const auto sameTickFineTune = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* fineTune = std::get_if<FineTune>(&event);
    return fineTune != nullptr && fineTune->tick == 5 && std::abs(fineTune->cents + 18.75) < 0.001;
  });
  expect(sameTickFineTune != events.end(),
         "KonamiSnes should re-emit the active fine tune before same-tick program/note playback");
  expect(std::distance(events.begin(), sameTickFineTune) < std::distance(events.begin(), programChange),
         "KonamiSnes fine tune should be ordered before the same-tick program change");
}

void konamiSnesLegacyObservedVibratoRateUsesGlobalTempoCeiling() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V2, {
                                                 {
                                                     0xea,
                                                     0x37,  // tempo 55
                                                     0xe4,
                                                     0x00,
                                                     0x2d,
                                                     0x63,  // active legacy vibrato, rate step 45
                                                     0xff,
                                                 },
                                                 {
                                                     0xea,
                                                     0x78,  // tempo 120, seen by legacy as song-level export tempo
                                                     0xff,
                                                 },
                                             });

  const auto vibratoRate = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate &&
           modulation->amount > 0.0;
  });
  expect(vibratoRate != performance.tracks[0].events.end(),
         "KonamiSnes legacy vibrato should emit a rate modulation event");

  const auto& rate = std::get<ModulationPerformanceEvent>(*vibratoRate);
  const double baseHz = vibrato::baseHz(KONAMISNES_V2);
  const double fullRange = synthAmountFromHertzRange(baseHz, baseHz * vibrato::defaultMaxRateFactor(KONAMISNES_V2));
  const double expectedCurrent = synthAmountFromHertzRange(baseHz, baseHz * (0x2d * 0x37)) / fullRange;
  const double expectedCeiling = synthAmountFromHertzRange(baseHz, baseHz * (0x2d * 0x78)) / fullRange;
  expect(std::abs(rate.amount - expectedCurrent) < 0.0001,
         "KonamiSnes legacy vibrato rate should keep the current track-tempo amount");
  expect(rate.controllerRangeMaxAmount && std::abs(*rate.controllerRangeMaxAmount - expectedCeiling) < 0.0001,
         "KonamiSnes legacy vibrato rate ceiling should include the sequence-global tempo range");
}

void konamiSnesPercussionUsesPackedGsDrumBank() {
  constexpr std::array<u8, 2> bytes{
      0x60,  // percussion on
      0xff,
  };

  const PerformanceSequence performance = renderKonamiSnesTrack(bytes);
  const MidiSequence midi = renderMidiSequence(performance);
  const auto& events = midi.tracks[0].events;

  const auto drumBank = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* bank = std::get_if<BankSelect>(&event);
    return bank != nullptr && bank->tick == 0;
  });
  expect(drumBank != events.end(), "KonamiSnes percussion should emit a drum bank select");
  expect(std::get<BankSelect>(*drumBank).bank == (0x7f << 7),
         "KonamiSnes percussion should use the packed GS bank field so MIDI serializes bank MSB 127");
}

void konamiSnesCompilerCursorDecodesVersionedFlowAndTruncation() {
  const std::vector<u8> flowBytes{
      0xfc, 0x08, 0x00, 0x0c, 0x00,        // V1 jump plus alternate discovery target
      0xff, 0x00, 0x00, 0xfe, 0x0c, 0x00,  // call alternate target
      0xff,                                // return from the call
      0xff,                                // alternate target
  };
  const TrackProgram flow = decodeKonamiSnesSourceTrack(ByteReader(SourceId{30}, flowBytes), KONAMISNES_V1, 0, 0);
  const auto conditionalIndex = flow.addressIndex.find(Address{0});
  const auto callIndex = flow.addressIndex.find(Address{8});
  expect(conditionalIndex && callIndex, "Konami compiler decoding should retain reachable branch and call blocks");
  const SourceCommand& conditional = flow.commands[*conditionalIndex];
  const SourceCommand& call = flow.commands[*callIndex];
  expect(conditional.flow.staticTargets.size() == 2 && conditional.flow.staticTargets[0].value == 8 &&
             conditional.flow.staticTargets[1].value == 12,
         "Konami conditional jump should expose both decoded branch targets");
  expect(call.flow.callTarget() && call.flow.staticTargets.front().value == 12,
         "Konami call should expose its decoded little-endian target");
  expect(std::ranges::all_of(flow.commands, [](const SourceCommand& command) { return command.encodedSize != 0; }),
         "valid Konami compiler commands should retain semantic IR and source ranges");

  const std::vector<u8> truncatedBytes{0xe4, 0x01, 0x20};
  std::vector<Diagnostic> diagnostics;
  const TrackProgram truncated =
      decodeKonamiSnesSourceTrack(ByteReader(SourceId{31}, truncatedBytes), KONAMISNES_V6, 0, 0, nullptr, &diagnostics);
  expect(truncated.commands.size() == 1 && truncated.commands[0].flow.terminal &&
             !truncated.commands[0].execution.valid() && truncated.commands[0].range.size == 3,
         "truncated Konami commands should keep their partial source range but no executable behavior");
  expect(!diagnostics.empty() && diagnostics.front().code == "truncated-record",
         "truncated Konami operands should use the shared compiler-cursor diagnostic");
}

void konamiSnesCompilerCursorUsesVersionedOperandLengths() {
  const auto firstSize = [](KonamiSnesVersion version, std::vector<u8> bytes) {
    const TrackProgram track = decodeKonamiSnesSourceTrack(ByteReader(SourceId{32}, bytes), version, 0, 0);
    return track.commands.front().encodedSize;
  };

  expect(firstSize(KONAMISNES_V1, {0xf3, 0x00, 0x02, 0x40, 0xff}) == 4,
         "V1 pitch slide should use its four-byte command layout");
  expect(firstSize(KONAMISNES_V2, {0xf3, 0x00, 0x00, 0x40, 0xff}) == 4,
         "zero-length V2 pitch slide should omit reserved and delta operands");
  expect(firstSize(KONAMISNES_V2, {0xf3, 0x00, 0x02, 0x40, 0x00, 0x34, 0x12, 0xff}) == 7,
         "active V2 pitch slide should include reserved and delta operands");
  expect(firstSize(KONAMISNES_V6, {0xf3, 0x00, 0x02, 0x40, 0x34, 0x12, 0xff}) == 6,
         "late pitch slide should use its six-byte command layout");
  expect(firstSize(KONAMISNES_V1, {0x63, 0xaa, 0xff}) == 2 && firstSize(KONAMISNES_V6, {0x63, 0xff}) == 1,
         "unknown low opcodes should retain their version-dependent operand lengths");
}

void konamiSnesEveryVersionRendersSourceFreeCommands() {
  constexpr std::array<KonamiSnesVersion, 6> versions{
      KONAMISNES_V1, KONAMISNES_V2, KONAMISNES_V3, KONAMISNES_V4, KONAMISNES_V5, KONAMISNES_V6,
  };
  for (const KonamiSnesVersion version : versions) {
    const PerformanceSequence performance =
        renderKonamiSnesProgram(version, {{0xea, 0x80, 0x3c, 0x03, 0x7f, 0x7f, 0xe0, 0x02, 0xff}});
    expect(performance.diagnostics.empty() && performance.tracks.size() == 1 && performance.tracks[0].endTick == 5,
           "every Konami engine version should render the common tempo/note/rest command path");
  }
}

void konamiSnesSequenceSimulationPreservesDriverVibratoDepth() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0xea, 0x80,              // tempo
                                               0xe4, 0x00, 0x40, 0x07,  // Axelay-style vibrato
                                               0xe0, 0x04, 0xff}});
  expect(performance.diagnostics.empty(), "KonamiSnes vibrato fixture should render without diagnostics");

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  s16 maximumBend = 0;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* bend = std::get_if<PitchBend>(&event)) {
      maximumBend = std::max<s16>(maximumBend, static_cast<s16>(std::abs(bend->value)));
    }
  }

  // Axelay's driver turns depth 7 into a peak offset of 7/32 semitones.
  // With MIDI's two-semitone bend range, that offset is a bend value of 896.
  expect(maximumBend == 896, "KonamiSnes sequence simulation should preserve the driver's full vibrato depth");
}

void konamiSnesCompiledPlaybackHandlesCallsLoopsTiesAndSlides() {
  const PerformanceSequence called = renderKonamiSnesProgram(KONAMISNES_V6, {{0xfe, 0x06, 0x00,  // call note subroutine
                                                                              0xe0, 0x02,        // rest after return
                                                                              0xff, 0x3c, 0x03, 0x7f, 0x7f, 0xff}});
  expect(called.diagnostics.empty() && called.tracks[0].endTick == 5,
         "compiled Konami call and context-sensitive end/return should preserve timing");
  expect(std::ranges::count_if(
             called.tracks[0].events,
             [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); }) == 1,
         "compiled Konami call should execute its decoded subroutine exactly once");

  const PerformanceSequence looped =
      renderKonamiSnesProgram(KONAMISNES_V6,
                              {{0xe6,                    // loop starts at the following note
                                0x3c, 0x04, 0x7f, 0x40,  // full-length note
                                0xe7, 0x02, 0x01, 0x01,  // play twice and change volume/pitch on replay
                                0xff}});
  const auto noteCount = std::ranges::count_if(looped.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(looped.diagnostics.empty() && looped.tracks[0].endTick == 8 && noteCount == 2,
         "both Konami loop counter state and accumulated replay changes should execute through the shared VM");

  const PerformanceSequence looped2 =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xe8, 0x3c, 0x04, 0x7f, 0x40, 0xe9, 0x02, 0x00, 0x00, 0xff}});
  expect(looped2.diagnostics.empty() && looped2.tracks[0].endTick == 8 &&
             std::ranges::count_if(looped2.tracks[0].events,
                                   [](const PerformanceEvent& event) {
                                     return std::holds_alternative<NotePerformanceEvent>(event);
                                   }) == 2,
         "the second Konami loop counter should remain independent and replay through the shared VM");

  const PerformanceSequence volta =
      renderKonamiSnesProgram(KONAMISNES_V1, {{0xf6,                    // shared section starts here
                                               0x3c, 0x01, 0x64, 0x7f,  // shared note
                                               0xf7,                    // first ending starts
                                               0x3d, 0x01, 0x64, 0x7f,  // first-ending note
                                               0xf7,                    // replay shared section
                                               0x3e, 0x01, 0x64, 0x7f,  // second-ending note
                                               0xf7, 0xff}});           // replay shared section, then exit
  std::vector<double> voltaKeys;
  for (const PerformanceEvent& event : volta.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      voltaKeys.push_back(note->key);
    }
  }
  const std::vector<double> expectedVoltaKeys{60.0, 61.0, 60.0, 62.0, 60.0};
  expect(volta.diagnostics.empty() && volta.tracks[0].endTick == 5 && voltaKeys == expectedVoltaKeys,
         "Konami V1 volta markers should replay shared notes and select each ending in order");

  const PerformanceSequence tied =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0x3c, 0x04, 0x7f, 0x7f,  // full-length note enables slur
                                               0xbc, 0xff,              // compressed same note extends it
                                               0xe1, 0x02, 0x7f,        // explicit tie extends it again
                                               0xe0, 0x03,              // rest breaks the chain
                                               0xec, 0x02, 0xf2, 0x10,  // transpose and fine tuning
                                               0x3e, 0x02, 0x40, 0x7f,  // note with an inline late-engine slide
                                               0xf3, 0x00, 0x02, 0x40, 0, 0, 0xff}});
  expect(tied.diagnostics.empty() && tied.tracks[0].endTick == 15,
         "compressed notes, ties, rests, and inline pitch slides should preserve their combined wait time");
  const auto transition = std::ranges::find_if(tied.tracks[0].automations, [](const PerformanceAutomation& automation) {
    return pitchTransitionIntent(automation) != nullptr;
  });
  expect(transition != tied.tracks[0].automations.end() &&
             std::holds_alternative<SampledAutomationCurve>(pitchTransitionIntent(*transition)->curve) &&
             std::get<SampledAutomationCurve>(pitchTransitionIntent(*transition)->curve).samples.size() >= 3 &&
             std::ranges::none_of(tied.tracks[0].events,
                                  [](const PerformanceEvent& event) {
                                    return std::holds_alternative<PitchBendPerformanceEvent>(event);
                                  }),
         "inline pitch slide should retain typed intent and its exact driver-tick curve");

  const MidiSequence exactPitchMidi = renderMidiSequence(tied);
  expect(std::ranges::any_of(exactPitchMidi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bend = std::get_if<PitchBend>(&event);
                               return bend != nullptr && bend->value != 0;
                             }),
         "KonamiSnes should preserve its exact sampled curve as pitch bend by default");

  const MidiSequence nativePitchMidi =
      renderMidiSequence(tied, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::any_of(nativePitchMidi.tracks[0].events,
                             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(nativePitchMidi.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }) &&
             std::ranges::any_of(nativePitchMidi.diagnostics,
                                 [](const Diagnostic& diagnostic) {
                                   return diagnostic.severity == Severity::Warning &&
                                          diagnostic.message.find("exact sampled pitch curve") != std::string::npos;
                                 }),
         "the same sampled transition should support a requested native-portamento approximation with a warning");
}

void konamiSnesCompiledAutomationTicksFades() {
  const PerformanceSequence performance =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xea, 0x80,              // tempo
                                               0xee, 0xff,              // volume
                                               0xe3, 0x14,              // pan
                                               0xe4, 0x00, 0x20, 0x10,  // vibrato
                                               0xeb, 0x70, 0xfc,        // tempo fade by negative fixed step
                                               0xef, 0xc0, 0xfc,        // volume fade
                                               0xf8, 0x10, 0xff,        // pan fade
                                               0xe0, 0x08, 0xff}});
  const auto& events = performance.tracks[0].events;
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 8,
         "compiled Konami fades should advance only through the waiting command");
  expect(performance.tracks[0].automations.size() >= 3 &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* tempo = std::get_if<TempoPerformanceEvent>(&event);
                                   return tempo != nullptr && tempo->header.tick > 0;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* level = std::get_if<LevelPerformanceEvent>(&event);
                                   return level != nullptr && level->header.tick > 0;
                                 }) &&
             std::ranges::any_of(events,
                                 [](const PerformanceEvent& event) {
                                   const auto* pan = std::get_if<PanPerformanceEvent>(&event);
                                   return pan != nullptr && pan->header.tick > 0;
                                 }),
         "tempo, volume, and pan fades should retain structured intent and exact per-tick realizations");
}

void konamiSnesPlayOnceCoordinatesGlobalLoopCompletion() {
  const PerformanceSequence performance = renderKonamiSnesProgram(
      KONAMISNES_V6, {
                         {0xe6, 0xe0, 0x04, 0xe7, 0x00, 0x01, 0x01},  // declared loop ignores finite-loop deltas
                         {0xe0, 0x0a, 0xff},                          // non-looping track ends at tick ten
                     });
  expect(performance.diagnostics.empty() && performance.tracks.size() == 2 && performance.tracks[0].endTick == 10 &&
             performance.tracks[1].endTick == 10,
         "play-once rendering should coordinate a Konami global loop boundary across all tracks");
  expect(std::ranges::none_of(
             performance.tracks[0].events,
             [](const PerformanceEvent& event) { return std::holds_alternative<TuningPerformanceEvent>(event); }),
         "declared Konami loops should not apply finite-loop pitch or volume deltas");

  const PerformanceSequence repeated =
      renderKonamiSnesProgram(KONAMISNES_V6, {{0xe6, 0x3c, 0x04, 0x40, 0x7f, 0xe7, 0x00, 0x00, 0x00}}, 1);
  expect(repeated.diagnostics.empty() && repeated.tracks[0].endTick == 8 &&
             std::ranges::count_if(repeated.tracks[0].events,
                                   [](const PerformanceEvent& event) {
                                     return std::holds_alternative<NotePerformanceEvent>(event);
                                   }) == 2,
         "requested Konami sequence loops should replay the declared loop through shared loop policy");
  const MidiSequence repeatedMidi = renderMidiSequence(repeated);
  expect(std::ranges::count_if(repeatedMidi.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); }) == 2,
         "requested Konami loop playback should remain visible in default MIDI output");
}
