/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"
#include "value/export/CollectionStitch.h"

namespace {

struct StitchProgramState {
  StitchProgramState(const SequenceProgram&, bool dirtyMidiState) : leaveDirtyMidiState(dirtyMidiState) {}

  void finalizePerformance(PerformanceSequence& performance) const {
    for (auto& track : performance.tracks) {
      auto position = track.events.insert(track.events.begin(), InstrumentPerformanceEvent{
                                                                    .bank = 127,
                                                                    .program = 0,
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

  test::SessionSnapshotBuilder builder;
  for (u32 index = 0; index < 2; ++index) {
    const SequenceProgramConfig& config = index == 0 ? config48 : config96;
    TrackProgram track{.sourceTrackNumber = index, .startAddress = Address{0}};
    const std::array<u8, 3> note{0x90, static_cast<u8>(0x3c + index), 0x04};
    const std::array<u8, 1> end{0xff};
    addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(index * 4, note.size()), note);
    addProbeCommand<ProbeEndCommand>(track, config, Address{3}, probeRange(index * 4 + 3, end.size()), end);

    const AssetId sequenceId{index * 3};
    const AssetId instrumentId{index * 3 + 1};
    const AssetId samplesId{index * 3 + 2};
    builder.assets.emplace_back(SequenceProgramAsset{
        .metadata = AssetMetadata{.id = sequenceId, .format = "Probe", .name = "Part " + std::to_string(index)},
        .program =
            SequenceProgram{
                .runtime = probeSequenceRuntime(),
                .timebase = config.timebase,
                .behavior = config.behavior,
                .tracks = {track},
            },
    });
    builder.assets.emplace_back(InstrumentSetAsset{
        .metadata = AssetMetadata{.id = instrumentId, .format = "Probe"},
        .instruments =
            {
                Instrument{
                    .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
                    .name = "Melodic " + std::to_string(index),
                    .regions = {Region{.sample = SampleRef{.index = 1}}},
                },
                Instrument{
                    .explicitAddress = InstrumentAddress{.bank = 127, .program = 0},
                    .name = "Drums " + std::to_string(index),
                    .regions = {Region{.sample = SampleRef{.index = 0}}},
                },
            },
    });
    builder.assets.emplace_back(SampleCollectionAsset{
        .metadata = AssetMetadata{.id = samplesId, .format = "Probe"},
        .samples =
            SampleCollection{
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
        .key = CollectionKey{.resolver = "ProbeSequence"},
        .members =
            {
                .sequence = sequenceId,
                .instrumentSets = {instrumentId},
                .sampleCollections = {samplesId},
            },
    });
  }

  FormatRegistry formats;
  auto module = probeSequenceModule();
  module.bindCollection = [](CollectionBindingContext& context) {
    const bool leaveDirtyMidiState = context.sequence != nullptr && context.sequence->metadata.name == "Part 0";
    context.sequenceRuntime = makeCompiledRuntime<ProbeCompilerCursor, StitchProgramState>(leaveDirtyMidiState);
  };
  formats.add(std::move(module));
  const std::array collections{CollectionId{0}, CollectionId{1}};
  const auto snapshot = builder.finish();
  const ExportRequest request{.dynamicEnvelopes = DynamicEnvelopePolicy::InstrumentVariants};
  const auto result = stitchCollections(snapshot, sources, collections, request, formats);

  auto restrictedRequest = request;
  restrictedRequest.exportOnlyUsedInstruments = true;
  const auto restricted = stitchCollections(snapshot, sources, collections, restrictedRequest, formats);
  auto observedRequest = request;
  observedRequest.modulationScaling = ModulationScalingPolicy::ObservedSequenceRange;
  const auto observed = stitchCollections(snapshot, sources, collections, observedRequest, formats);

  expect(result.complete(), "stitching should compact sparse source banks instead of reserving their numeric gaps");
  expect(restricted.complete(), "used-only stitched export should retain each part's playable instrument data");
  expect(observed.complete(), "observed-range modulation scaling should support stitched exports");
  expect(result.parts.size() == 2 && result.parts[0].startTick == 0 && result.parts[1].startTick == 8,
         "stitched parts should be rescaled to a common PPQN and placed sequentially");
  expect(result.parts[0].banks ==
                 std::vector<CollectionStitchBank>({{.source = 0, .target = 0}, {.source = 127, .target = 1}}) &&
             result.parts[1].banks ==
                 std::vector<CollectionStitchBank>({{.source = 0, .target = 2}, {.source = 127, .target = 3}}),
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
         "source bank 127 selections should follow the same compact mapping as the SoundFont presets");
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
             soundFontPgenContainsAmount(result.soundFont.bytes, 34, 30368),
         "instrument-variant policy should express dynamic ADSR differences on shared SoundFont presets");
  expect(chunkSize(restricted.soundFont.bytes, "phdr") == 3 * 38 &&
             chunkSize(restricted.soundFont.bytes, "shdr") == 3 * 46 &&
             soundFontIgenContainsAmount(restricted.soundFont.bytes, 34, -2400) &&
             !containsAscii(restricted.soundFont.bytes, "Unused Sample"),
         "used-only stitching should keep only the selected dynamic variants and their referenced samples");
}

}  // namespace

void runValueCollectionStitchTests() {
  stitchedExportCompactsBanksAndHonorsInstrumentPolicies();
}
