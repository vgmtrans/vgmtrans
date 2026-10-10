/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"
#include "SessionSnapshotBuilder.h"
#include "SessionTestSupport.h"
#include "SynthExportTestSupport.h"

#include "value/export/CollectionStitch.h"
#include "value/export/midi/PerformanceMidiRenderer.h"

#include <array>
#include <set>

using namespace vgmtrans::core;

namespace {

struct EncodedMessage {
  u64 tick;
  std::vector<u8> bytes;
};

// Inspect the serialized track, including ordering at a stitched boundary.
std::vector<EncodedMessage> trackMessages(const std::vector<u8>& bytes, size_t trackIndex) {
  const auto be32 = [&](size_t at) {
    return (u32{bytes.at(at)} << 24) | (u32{bytes.at(at + 1)} << 16) |
           (u32{bytes.at(at + 2)} << 8) | bytes.at(at + 3);
  };
  size_t cursor = 8 + be32(4);
  for (size_t index = 0; index < trackIndex; ++index) cursor += 8 + be32(cursor + 4);
  const size_t end = cursor + 8 + be32(cursor + 4);
  cursor += 8;
  const auto variableLength = [&]() {
    u64 value = 0;
    u8 byte;
    do {
      expect(cursor < end, "MIDI variable-length value must fit its track");
      byte = bytes.at(cursor++);
      value = (value << 7) | (byte & 0x7f);
    } while (byte & 0x80);
    return value;
  };
  std::vector<EncodedMessage> messages;
  u64 tick = 0;
  while (cursor < end) {
    tick += variableLength();
    const size_t begin = cursor;
    const u8 status = bytes.at(cursor++);
    if (status == 0xff || status == 0xf0) {
      if (status == 0xff) ++cursor;
      const u64 size = variableLength();
      cursor += size;
    } else {
      expect(status >= 0x80 && status < 0xf0, "expected an explicit channel status in exported MIDI");
      cursor += (status & 0xe0) == 0xc0 ? 1 : 2;
    }
    expect(cursor <= end, "MIDI message must fit its track");
    messages.push_back({tick, {bytes.begin() + begin, bytes.begin() + cursor}});
  }
  return messages;
}

struct StitchProgramState {
  StitchProgramState(const SequenceProgram&, bool dirtyMidiState) : leaveDirtyMidiState(dirtyMidiState) {}

  void finalizePerformance(PerformanceSequence& performance) const {
    for (auto& track : performance.tracks) {
      auto position =
          track.events.insert(track.events.begin(), InstrumentPerformanceEvent{
                                                        .instrument = InstrumentAddress{.bank = 255, .program = 0},
                                                        .forceBankSelect = true,
                                                    });
      position = track.events.insert(
          ++position, EnvelopePerformanceEvent{
                          .update = EnvelopeUpdate::set(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack),
                      });
      track.events.insert(++position, ModulationPerformanceEvent{
                                          .target = ModulationPerformanceTarget::VibratoDepth,
                                          .amount = leaveDirtyMidiState ? 0.25 : 0.5,
                                      });
      if (leaveDirtyMidiState) {
        track.events.push_back(ModulationPerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .target = ModulationPerformanceTarget::VibratoDepth,
            .amount = 1.0,
            .pitchDepthSemitones = 1.0,
        });
        track.events.push_back(ModulationPerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .target = ModulationPerformanceTarget::VibratoRate,
            .amount = 1.0,
            .context = LfoPerformanceContext{.frequencyHz = 5.0},
        });
        track.events.push_back(ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 1},
                                                          .target = ModulationPerformanceTarget::VibratoDelay,
                                                          .context = {.delay = LfoDelay{.milliseconds = 100.0}}});
        track.events.push_back(ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 2},
                                                          .target = ModulationPerformanceTarget::VibratoDelay,
                                                          .context = {.delay = LfoDelay{.milliseconds = 150.0}}});
        track.events.push_back(ModulationPerformanceEvent{.header = PerformanceEventHeader{.tick = 3},
                                                          .target = ModulationPerformanceTarget::VibratoDelay,
                                                          .context = {.delay = LfoDelay{.milliseconds = 200.0}}});
        track.events.push_back(TuningPerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .cents = 25.0,
        });
        track.events.push_back(PitchBendRangePerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .cents = 1200,
        });
        track.events.push_back(PitchBendPerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .semitones = 6.0,
        });
        track.events.push_back(PortamentoEnablePerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .enabled = true,
        });
        track.events.push_back(LegatoPedalPerformanceEvent{
            .header = PerformanceEventHeader{.tick = 1},
            .enabled = true,
        });
      }
    }
  }

  bool leaveDirtyMidiState = false;
};

void stitchedExportCompactsBanksAndHonorsInstrumentPolicies() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "stitch.pcm"}, {0, 0, 0, 0});
  const SequenceProgramConfig config48 = probeSequenceConfig();
  SequenceProgramConfig config96 = config48;
  config96.timebase.ppqn = 96;
  std::array<u32, 2> preparations{};
  const SequencePreparer binder = [&preparations](SequencePreparationContext& context) {
    const bool leaveDirtyMidiState = context.sequence.metadata.name == "Part 0";
    ++preparations[leaveDirtyMidiState ? 0 : 1];
    return makeCompiledRuntime<ProbePlayback, StitchProgramState>(leaveDirtyMidiState);
  };

  test::SessionSnapshotBuilder builder;
  for (u32 index = 0; index < 2; ++index) {
    const SequenceProgramConfig& config = index == 0 ? config48 : config96;
    TrackProgram track{.streams = {{.channels = {index}}}, .startAddress = Address{0}};
    const std::array<u8, 3> note{0x90, static_cast<u8>(0x3c + index), 0x04};
    const std::array<u8, 1> end{0xff};
    addProbeCommand(track, Address{0}, probeRange(index * 4, note.size()), note);
    addProbeCommand(track, Address{3}, probeRange(index * 4 + 3, end.size()), end);

    const AssetId sequenceId{index * 3};
    const AssetId instrumentId{index * 3 + 1};
    const AssetId samplesId{index * 3 + 2};
    builder.assets.emplace_back(SequenceProgramAsset{
        .metadata = AssetMetadata{.id = sequenceId, .format = "Probe", .name = "Part " + std::to_string(index)},
        .program =
            SequenceProgram{
                .runtime = makeCompiledRuntime<ProbePlayback, StitchProgramState>(false),
                .timebase = config.timebase,
                .behavior = config.behavior,
                .tracks = {track},
            },
        .prepare = binder,
    });
    builder.assets.emplace_back(SoundBankAsset{
        .metadata = AssetMetadata{.id = instrumentId, .format = "Probe"},
        .instruments =
            {
                Instrument{
                    .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
                    .name = "Melodic " + std::to_string(index),
                    .regions = {Region{.sample = SampleRef::resolved(samplesId, 1)}},
                },
                Instrument{
                    .explicitAddress = InstrumentAddress{.bank = 255, .program = 0},
                    .name = "Drums " + std::to_string(index),
                    .regions = {Region{.sample = SampleRef::resolved(samplesId, 0)}},
                },
            },
    });
    builder.assets.emplace_back(SamplePoolAsset{
        .metadata = AssetMetadata{.id = samplesId, .format = "Probe"},
        .pool =
            SamplePool{
                .samples =
                    {
                        Sample{
                            .name = "Used Sample " + std::to_string(index),
                            .codec = AudioCodec::PcmS8,
                            .encodedData = SourceRange{.source = source, .offset = index * 2, .size = 1},
                            .sampleRate = 16000,
                        },
                        Sample{
                            .name = "Unused Sample " + std::to_string(index),
                            .codec = AudioCodec::PcmS8,
                            .encodedData = SourceRange{.source = source, .offset = index * 2 + 1, .size = 1},
                            .sampleRate = 16000,
                        },
                    },
            },
    });
    builder.collections.push_back(Collection{
        .id = CollectionId{index},
        .name = "Part " + std::to_string(index),
        .selection =
            {
                .sequence = sequenceId,
                .soundBanks = {instrumentId},
                .samplePools = {samplesId},
            },
      .inputs = {.banks = {{.bank = instrumentId}}},
    });
  }

  const std::array collections{CollectionId{0}, CollectionId{1}};
  const auto snapshot = builder.finish();
  const ExportRequest request{.dynamicEnvelopes = DynamicEnvelopePolicy::InstrumentVariants};
  const auto result = stitchCollections(snapshot, sources, collections, request);

  auto restrictedRequest = request;
  restrictedRequest.exportOnlyUsedInstruments = true;
  const auto restricted = stitchCollections(snapshot, sources, collections, restrictedRequest);
  const auto beforeRepeat = preparations;
  const std::array repeatedCollections{CollectionId{0}, CollectionId{1}, CollectionId{0}};
  const auto repeated = stitchCollections(snapshot, sources, repeatedCollections, restrictedRequest);
  expect(repeated.complete() && repeated.parts.size() == 3 &&
             repeated.parts[0].banks == repeated.parts[2].banks &&
             repeated.soundFont.bytes == restricted.soundFont.bytes &&
             preparations == std::array<u32, 2>{beforeRepeat[0] + 1, beforeRepeat[1] + 1},
         "repeating a collection must prepare it once and reuse its instruments, plan and companion presets");
  auto observedRequest = request;
  observedRequest.modulationScaling = ModulationScalingPolicy::ObservedSequenceRange;
  const auto observed = stitchCollections(snapshot, sources, collections, observedRequest);

  expect(result.complete(), "stitching should compact sparse source banks instead of reserving their numeric gaps");
  expect(restricted.complete(), "used-only stitched export should retain each part's playable instrument data");
  expect(observed.complete(), "observed-range modulation scaling should support stitched exports");
  expect(result.parts.size() == 2 && result.parts[0].startTick == 0 && result.parts[1].startTick == 8,
         "stitched parts should be rescaled to a common PPQN and placed sequentially");
  expect(result.parts[0].banks ==
                 std::vector<CollectionStitchBank>({{.source = 0, .target = 0}, {.source = 255, .target = 1}}) &&
             result.parts[1].banks ==
                 std::vector<CollectionStitchBank>({{.source = 0, .target = 2}, {.source = 255, .target = 3}}),
         "each collection's distinct source banks should receive dense, non-overlapping target banks");

  const std::vector<u8> secondPartBank{0xb0, 0x00, 0x02};
  expect(std::search(result.midi.bytes.begin(), result.midi.bytes.end(), secondPartBank.begin(),
                     secondPartBank.end()) != result.midi.bytes.end(),
         "a later part that uses the default source bank should select its compacted target bank");
  const std::vector<u8> firstPartDrums{0xb0, 0x00, 0x01};
  const std::vector<u8> secondPartDrums{0xb0, 0x00, 0x03};
  expect(std::search(result.midi.bytes.begin(), result.midi.bytes.end(), firstPartDrums.begin(),
                     firstPartDrums.end()) != result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), secondPartDrums.begin(),
                         secondPartDrums.end()) != result.midi.bytes.end(),
         "source bank 255 selections should follow the same compact mapping as the SoundFont presets");
  // The first part's 150 ms delay maps to 74 within its observed 100-200 ms range.
  const std::vector<u8> unscaledVibratoDelay{0xb0, 0x4e, 74};
  expect(std::search(result.midi.bytes.begin(), result.midi.bytes.end(), unscaledVibratoDelay.begin(),
                     unscaledVibratoDelay.end()) != result.midi.bytes.end(),
         "stitching should not PPQN-scale normalized MIDI delay-controller data");
  const std::vector<u8> scaledFirstPartVibrato{0xb0, 0x01, 0x40};
  const std::vector<u8> scaledSecondPartVibrato{0xb0, 0x01, 0x7f};
  expect(std::search(observed.midi.bytes.begin(), observed.midi.bytes.end(), scaledFirstPartVibrato.begin(),
                     scaledFirstPartVibrato.end()) != observed.midi.bytes.end() &&
             std::search(observed.midi.bytes.begin(), observed.midi.bytes.end(), scaledSecondPartVibrato.begin(),
                         scaledSecondPartVibrato.end()) != observed.midi.bytes.end(),
         "observed modulation scaling should use one range across all stitched parts");
  const std::vector<u8> centeredPitchBend{0xe0, 0x00, 0x40};
  const std::vector<u8> defaultPitchBendRange{0xb0, 0x06, 0x02};
  const std::vector<u8> centeredFineOrCoarseTune{0xb0, 0x06, 0x40};
  const std::vector<u8> portamentoOn{0xb0, 0x41, 0x7f};
  const std::vector<u8> portamentoOff{0xb0, 0x41, 0x00};
  const std::vector<u8> legatoOn{0xb0, 0x44, 0x7f};
  const std::vector<u8> legatoOff{0xb0, 0x44, 0x00};
  expect(std::search(result.midi.bytes.begin(), result.midi.bytes.end(), portamentoOn.begin(), portamentoOn.end()) !=
                 result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), legatoOn.begin(), legatoOn.end()) !=
                 result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), centeredPitchBend.begin(),
                         centeredPitchBend.end()) != result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), defaultPitchBendRange.begin(),
                         defaultPitchBendRange.end()) != result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), centeredFineOrCoarseTune.begin(),
                         centeredFineOrCoarseTune.end()) != result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), portamentoOff.begin(),
                         portamentoOff.end()) != result.midi.bytes.end() &&
             std::search(result.midi.bytes.begin(), result.midi.bytes.end(), legatoOff.begin(), legatoOff.end()) !=
                 result.midi.bytes.end(),
         "a later stitched part should start from clean pitch, tuning, portamento, and legato state");

  const size_t presets = asciiOffset(result.soundFont.bytes, "phdr") + 8;
  std::set<u16> banks;
  for (size_t index = 0; index < 6; ++index) {
    banks.insert(readLe16(result.soundFont.bytes, presets + index * 38 + 22));
  }
  expect(banks == std::set<u16>({0, 1, 2, 3}),
         "the merged SoundFont should use the same dense bank mapping as the MIDI");
  expect(
      containsAscii(result.soundFont.bytes, "Used Sample 0") && containsAscii(result.soundFont.bytes, "Used Sample 1"),
      "collection-relative sample references should remain bound to their original sample banks");
  expect(chunkSize(result.soundFont.bytes, "phdr") == 7 * 38 && chunkSize(result.soundFont.bytes, "shdr") == 5 * 46 &&
             soundFontGeneratorContains(result.soundFont.bytes, "pgen", 34, 30368),
         "instrument-variant policy should express dynamic ADSR differences on shared SoundFont presets");
  expect(chunkSize(restricted.soundFont.bytes, "phdr") == 3 * 38 &&
             chunkSize(restricted.soundFont.bytes, "shdr") == 3 * 46 &&
             soundFontGeneratorContains(restricted.soundFont.bytes, "igen", 34, -2400) &&
             !containsAscii(restricted.soundFont.bytes, "Unused Sample"),
         "used-only stitching should keep only the selected dynamic variants and their referenced samples");
}

struct BoundaryProgramState {
  BoundaryProgramState(const SequenceProgram&, bool explicitState) : explicitState(explicitState) {}

  void finalizePerformance(PerformanceSequence& performance) const {
    if (!explicitState) return;
    auto& conductor = performance.tracks.front();
    conductor.events.push_back(TempoPerformanceEvent{.microsecondsPerQuarter = 750000});
    conductor.events.push_back(TimeSignaturePerformanceEvent{.numerator = 3});
    for (size_t index = 0; index < performance.tracks.size(); ++index) {
      auto& track = performance.tracks[index];
      track.events.push_back(MonoModePerformanceEvent{.channels = 1});
      // The first channel on each port sets its master volume.
      if (index % 15 == 0) track.events.push_back(MasterLevelPerformanceEvent{.linearGain = 0.25});
    }
  }

  bool explicitState;
};

void stitchedPartsResetSongAndPortStateBeforeTheirOwnCommands() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "boundary.pcm"}, {0});
  for (const bool explicitNext : {false, true}) {
    test::SessionSnapshotBuilder builder;
    const AssetId bank{2};
    builder.assets.emplace_back(SoundBankAsset{
        .metadata = {.id = bank},
        .instruments = {Instrument{.explicitAddress = InstrumentAddress{},
                                    .regions = {Region{.sample = SampleRef::resolved(bank, 0)}}}},
        .localSamples = {.samples = {Sample{.codec = AudioCodec::PcmS8, .encodedData = {source, 0, 1},
                                            .sampleRate = 16000}}},
    });
    for (u32 part = 0; part < 2; ++part) {
      auto program = probeSequenceProgram();
      program.runtime = makeCompiledRuntime<ProbePlayback, BoundaryProgramState>(part == 0 || explicitNext);
      for (u32 channel = 0; channel < 17; ++channel) {
        TrackProgram track{.streams = {{.channels = {channel}}}, .startAddress = Address{0}};
        const std::array<u8, 3> note{0x90, 60, 4};
        const std::array<u8, 1> end{0xff};
        addProbeCommand(track, Address{0}, probeRange(0, 3), note);
        addProbeCommand(track, Address{3}, probeRange(3, 1), end);
        program.tracks.push_back(std::move(track));
      }
      builder.assets.emplace_back(SequenceProgramAsset{.metadata = {.id = AssetId{part}},
                                                        .program = std::move(program)});
      builder.collections.push_back(Collection{.id = CollectionId{part},
          .selection = {.sequence = AssetId{part}, .soundBanks = {bank}}, .inputs = {.banks = {{.bank = bank}}}});
    }
    const auto snapshot = builder.finish();
    for (const bool writePorts : {false, true}) {
      ExportRequest request;
      request.sequence.midi.writePortMetaEvents = writePorts;
      const std::array collections{CollectionId{0}, CollectionId{1}};
      const auto result = stitchCollections(snapshot, sources, collections, request);
      expect(result.complete() && result.parts[1].startTick == 4, "boundary fixture must export both parts");
      for (size_t index = 0; index < 17; ++index) {
        const auto messages = trackMessages(result.midi.bytes, 17 + index);
        const auto matching = [&](std::initializer_list<u8> prefix) {
          std::vector<std::vector<u8>> found;
          for (const auto& message : messages) {
            if (message.tick == 4 && message.bytes.size() >= prefix.size() &&
                std::equal(prefix.begin(), prefix.end(), message.bytes.begin())) found.push_back(message.bytes);
          }
          return found;
        };
        if (index == 0) {
          std::vector<std::vector<u8>> tempos{{0xff, 0x51, 3, 0x07, 0xa1, 0x20}};
          std::vector<std::vector<u8>> signatures{{0xff, 0x58, 4, 4, 2, 24, 8}};
          if (explicitNext) {
            tempos.push_back({0xff, 0x51, 3, 0x0b, 0x71, 0xb0});
            signatures.push_back({0xff, 0x58, 4, 3, 2, 24, 8});
          }
          expect(matching({0xff, 0x51}) == tempos && matching({0xff, 0x58}) == signatures,
                 "each part must restore default tempo and meter before its own same-tick settings");
        }
        const u8 slot = index % 15;
        const u8 status = 0xb0 | (slot >= 9 ? slot + 1 : slot);
        expect(matching({status, 127}) == std::vector<std::vector<u8>>{{status, 127, 0}},
               "a stitched channel must return to poly mode before its source commands");
        const auto poly = std::ranges::find(messages, std::vector<u8>{status, 127, 0}, &EncodedMessage::bytes);
        const auto mono = std::ranges::find(messages, std::vector<u8>{status, 126, 1}, &EncodedMessage::bytes);
        expect(!explicitNext || (mono != messages.end() && poly < mono),
               "an explicit mono command must override the boundary's poly reset");
        std::vector<std::vector<u8>> volumes;
        if (index == 0 || (writePorts && index == 15)) volumes.push_back({0xf0, 7, 0x7f, 0x7f, 4, 1, 127, 127, 0xf7});
        if (explicitNext && index % 15 == 0) volumes.push_back({0xf0, 7, 0x7f, 0x7f, 4, 1, 0, 64, 0xf7});
        expect(matching({0xf0, 7, 0x7f, 0x7f, 4, 1}) == volumes,
               "master volume must reset once per output port, before any source volume on that port");
      }
    }
  }
}

}  // namespace

void runValueCollectionStitchTests() {
  stitchedExportCompactsBanksAndHonorsInstrumentPolicies();
  stitchedPartsResetSongAndPortStateBeforeTheirOwnCommands();
}
