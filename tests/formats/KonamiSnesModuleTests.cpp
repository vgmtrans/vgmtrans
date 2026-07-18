/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnesLayout.h"
#include "value/formats/KonamiSnes/KonamiSnesModule.h"
#include "value/formats/KonamiSnes/KonamiSnesSequence.h"
#include "value/formats/KonamiSnes/KonamiSnesSynth.h"

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

bool hasGeneratorDestination(const Instrument& instrument, SynthDestination destination) {
  return std::ranges::any_of(instrument.generators, [destination](const SynthGenerator& generator) {
    return generator.destination == destination;
  });
}

bool hasModulatorDestination(const Instrument& instrument, SynthDestination destination) {
  return std::ranges::any_of(instrument.modulators, [destination](const SynthModulator& modulator) {
    return modulator.destination == destination;
  });
}

bool hasSourceLessModulatorDestination(const Instrument& instrument, SynthDestination destination) {
  return std::ranges::any_of(instrument.modulators, [destination](const SynthModulator& modulator) {
    return !modulator.source && modulator.destination == destination;
  });
}

std::vector<u8> makeKonamiSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 10> setSongHeaderAddressGG4{0x8f, 0x00, 0x0a, 0x8f, 0x20,
                                                       0x0b, 0xcd, 0x00, 0xd8, 0x1c};
  writeBytes(bytes, 0x0100, setSongHeaderAddressGG4);

  constexpr std::array<u8, 15> jumpToVcmdGG4{0x1c, 0xfd, 0xf6, 0xbc, 0x1a, 0x2d, 0xf6, 0xbb,
                                             0x1a, 0x2d, 0xf6, 0x00, 0x03, 0xf0, 0x08};
  writeBytes(bytes, 0x0120, jumpToVcmdGG4);
  writeLe16(bytes, 0x0120 + 11, 0x0300);

  constexpr std::array<u8, 6> setDirGG4{0x8f, 0x5d, 0xf2, 0x8f, 0x50, 0xf3};
  writeBytes(bytes, 0x0140, setDirGG4);

  constexpr std::array<u8, 48> loadInstrGG4{
      0x09, 0x11, 0x10, 0xfd, 0xf5, 0xa1, 0x01, 0xd0, 0x27, 0xdd, 0x68, 0x28,
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

PerformanceSequence renderKonamiSnesTrack(std::span<const u8> commandBytes) {
  std::vector<u8> bytes(commandBytes.begin(), commandBytes.end());
  const auto& descriptor = konamiSnesSequenceDescriptor(KONAMISNES_V6);
  TrackProgram track = decodeKonamiSnesSourceTrack(ByteReader(SourceId{9}, bytes), descriptor, 0, 0);
  const SequenceProgram program{
      .dialect = descriptor.dialect.id,
      .timebase = descriptor.dialect.timebase,
      .sourceBaseAddress = Address{0},
      .behavior = descriptor.dialect.defaultBehavior,
      .tracks = {track},
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program, descriptor.dialect);
}

PerformanceSequence renderKonamiSnesProgram(KonamiSnesVersion version, const std::vector<std::vector<u8>>& tracks) {
  const auto& descriptor = konamiSnesSequenceDescriptor(version);
  std::vector<TrackProgram> programTracks;
  programTracks.reserve(tracks.size());
  for (u32 trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
    programTracks.push_back(decodeKonamiSnesSourceTrack(ByteReader(SourceId{100 + trackIndex}, tracks[trackIndex]),
                                                        descriptor, trackIndex, 0));
  }
  const SequenceProgram program{
      .dialect = descriptor.dialect.id,
      .timebase = descriptor.dialect.timebase,
      .sourceBaseAddress = Address{0},
      .behavior = descriptor.dialect.defaultBehavior,
      .tracks = std::move(programTracks),
  };
  return SequenceVm(LoopPolicy::PlayOnce).render(program, descriptor.dialect);
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
  const SessionSnapshot project = session.scanPendingSources();
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
      "konami-snes.tempo", "konami-snes.program", "konami-snes.volume", "konami-snes.pan",
      "konami-snes.vibrato", "konami-snes.note", "konami-snes.end"};
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
  expect(vibratoDepth != performance.tracks[0].events.end(), "KonamiSnes vibrato command should emit target-neutral depth");
  const auto& vibratoDepthEvent = std::get<ModulationPerformanceEvent>(*vibratoDepth);
  const double expectedDepthCents = vibrato::currentDepthCents(KONAMISNES_V6, 0x10, 0x10 << 8);
  const double expectedDepthAmount =
      expectedDepthCents / vibrato::maxDepthCents(KONAMISNES_V6, kDefaultVibratoMaxDepth);
  expect(std::abs(vibratoDepthEvent.amount - expectedDepthAmount) < 0.0001,
         "KonamiSnes vibrato depth should be normalized against the full synth range");
  expect(vibratoDepthEvent.pitchDepthSemitones &&
             std::abs(*vibratoDepthEvent.pitchDepthSemitones - (expectedDepthCents / 400.0)) < 0.0001,
         "KonamiSnes vibrato depth should retain peak pitch swing for sequence-event simulation");
  const auto vibratoDelay = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<VibratoDelayPerformanceEvent>(event);
  });
  expect(vibratoDelay != performance.tracks[0].events.end(),
         "KonamiSnes vibrato command should emit target-neutral delay");
  expect(std::get<VibratoDelayPerformanceEvent>(*vibratoDelay).delayTicks == 2,
         "KonamiSnes vibrato delay should be converted to rendered sequence ticks");

  const MidiSequence synthModulationMidi = PerformanceMidiRenderer().render(performance);
  expect(hasMidiEvent<VibratoDepth>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoFrequency>(synthModulationMidi.tracks[0]) &&
             hasMidiEvent<VibratoDelay>(synthModulationMidi.tracks[0]),
         "default KonamiSnes MIDI rendering should preserve synth modulation controllers");

  const MidiSequence simulatedMidi = PerformanceMidiRenderer().render(
      performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
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
  expect(!instrument.modulators.empty(), "KonamiSnes instruments should carry default vibrato modulators");
  expect(hasGeneratorDestination(instrument, SynthDestination::VibratoDelay),
         "KonamiSnes instruments should carry a default vibrato delay generator");
  expect(hasSourceLessModulatorDestination(instrument, SynthDestination::VibratoDepth),
         "KonamiSnes instruments should carry default vibrato depth as a synth modulator");
  expect(hasModulatorDestination(instrument, SynthDestination::VibratoDelay),
         "KonamiSnes instruments should carry a default vibrato delay modulator");

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
  expect(instruments.front().index == 0 && instruments.front().address == 0x4000,
         "KonamiSnes parser should preserve the sparse source instrument index and address");
  const auto samples = parseKonamiSnesSampleInfos(ByteReader(SourceId{8}, bytes), *layout->spcDirAddress, instruments);
  expect(samples.size() == 1 && samples.front().srcn == 0 && samples.front().encodedLength == 9,
         "KonamiSnes sample parser should keep only samples used by valid instruments");
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
  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
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
  const PerformanceSequence performance = renderKonamiSnesProgram(
      KONAMISNES_V2,
      {
          {
              0xea, 0x37,              // tempo 55
              0xe4, 0x00, 0x2d, 0x63,  // active legacy vibrato, rate step 45
              0xff,
          },
          {
              0xea, 0x78,  // tempo 120, seen by legacy as song-level export tempo
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
  const double fullRange =
      synthAmountFromHertzRange(baseHz, baseHz * vibrato::defaultMaxRateFactor(KONAMISNES_V2));
  const double expectedCurrent =
      synthAmountFromHertzRange(baseHz, baseHz * (0x2d * 0x37)) / fullRange;
  const double expectedCeiling =
      synthAmountFromHertzRange(baseHz, baseHz * (0x2d * 0x78)) / fullRange;
  expect(std::abs(rate.amount - expectedCurrent) < 0.0001,
         "KonamiSnes legacy vibrato rate should keep the current track-tempo amount");
  expect(rate.controllerRangeMaxAmount &&
             std::abs(*rate.controllerRangeMaxAmount - expectedCeiling) < 0.0001,
         "KonamiSnes legacy vibrato rate ceiling should include the sequence-global tempo range");
}

void konamiSnesPercussionUsesPackedGsDrumBank() {
  constexpr std::array<u8, 2> bytes{
      0x60,  // percussion on
      0xff,
  };

  const PerformanceSequence performance = renderKonamiSnesTrack(bytes);
  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  const auto& events = midi.tracks[0].events;

  const auto drumBank = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* bank = std::get_if<BankSelect>(&event);
    return bank != nullptr && bank->tick == 0;
  });
  expect(drumBank != events.end(), "KonamiSnes percussion should emit a drum bank select");
  expect(std::get<BankSelect>(*drumBank).bank == (0x7f << 7),
         "KonamiSnes percussion should use the packed GS bank field so MIDI serializes bank MSB 127");
}
