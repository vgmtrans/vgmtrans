/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"
#include "DiagnosticTestSupport.h"
#include "SequenceTestSupport.h"
#include "SessionSnapshotBuilder.h"

#include "value/export/Export.h"
#include "value/export/midi/PerformanceMidiRenderer.h"

#include <algorithm>
#include <array>

using namespace vgmtrans::core;

namespace {

void exportRequestSequenceLoopsAffectMidiLowering() {
  expect(ExportRequest{}.sequence.sequenceLoops == 1,
         "the user-facing export request should default to one sequence loop");
  expect(ExportRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "collection export should default to native synth modulation");
  expect(PlaybackRequest{}.modulationConversion == ModulationConversionPolicy::SynthModulators,
         "backend-neutral playback requests should default to native synth modulation");
  expect(ExportRequest{}.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants &&
             PlaybackRequest{}.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants,
         "export and playback should materialize dynamic envelopes by default");
  expect(ExportRequest{}.sampleFiltering == SampleFilteringPolicy::FormatPreferred &&
             PlaybackRequest{}.sampleFiltering == SampleFilteringPolicy::FormatPreferred,
         "sample filtering should use each format's recommendation by default");
  expect(!ExportRequest{}.sequence.midi.terminatePreviousVoice,
         "previous-voice termination should remain explicitly opt-in");
  expect(ExportRequest{}.sequence.midi.tuning == MidiTuningRendering::PitchBend,
         "tuning should default to pitch-bend rendering");

  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x00, 0x00};
  addProbeCommand(track, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand(track, Address{3}, probeRange(3, jumpBytes.size()), jumpBytes);

  test::SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Looping Sequence",
          },
      .program =
          SequenceProgram{
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  });
  snapshotBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Looping",
      .members = {.sequence = AssetId{0}},
  });
  const SessionSnapshot project = snapshotBuilder.finish();

  SourceStore sources;

  const auto artifacts = exportCollection(project, sources, CollectionId{0},
                                          ExportRequest{
                                              .kinds = {ExportKind::Midi},
                                              .sequence =
                                                  {
                                                      .loopPolicy = LoopPolicy::PlayOnce,
                                                      .sequenceLoops = 2,
                                                  },
                                          });

  expect(artifacts.size() == 1 && artifacts[0].diagnostics.empty(),
         "MIDI export with configured sequence loops should produce one clean artifact");
  const auto noteOnCount = std::ranges::count(artifacts[0].bytes, static_cast<u8>(0x90));
  expect(noteOnCount == 3, "ExportRequest sequenceLoops should replay the loop before MIDI rendering");
}

void standaloneSequenceExportDoesNotRequireACollection() {
  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{
      .startAddress = Address{0},
  };

  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand(track, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand(track, Address{3}, probeRange(3, endBytes.size()), endBytes);

  test::SessionSnapshotBuilder snapshotBuilder;
  snapshotBuilder.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{7},
              .format = "Probe",
              .name = "Loose/Sequence",
          },
      .program =
          SequenceProgram{
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  });
  auto ambiguousBuilder = snapshotBuilder;
  ambiguousBuilder.collections = {
      Collection{.id = CollectionId{0}, .name = "First", .members = {.sequence = AssetId{7}}},
      Collection{.id = CollectionId{1}, .name = "Second", .members = {.sequence = AssetId{7}}},
  };
  const SessionSnapshot snapshot = snapshotBuilder.finish();
  expect(snapshot.collections().empty(), "standalone MIDI fixture should not contain a collection");

  const SourceStore sources;
  const Artifact artifact = exportSequenceMidi(snapshot, sources, AssetId{7}, SequenceExportRequest{});

  expect(artifact.filename == "Loose_Sequence.mid",
         "standalone sequence export should derive a safe filename from sequence metadata");
  expect(artifact.mediaType == "audio/midi", "standalone sequence export should identify Standard MIDI data");
  expect(artifact.diagnostics.empty(), "standalone sequence export should not require collection diagnostics");
  expect(artifact.bytes.size() > 14 && std::string(artifact.bytes.begin(), artifact.bytes.begin() + 4) == "MThd",
         "standalone sequence export should produce a Standard MIDI file");

  const Artifact ambiguous =
      exportSequenceMidi(ambiguousBuilder.finish(), sources, AssetId{7}, SequenceExportRequest{});
  expect(ambiguous.bytes.empty(), "direct sequence export should not choose the first of several collections");
  diagnosticWithMessage(ambiguous.diagnostics,
                        "Sequence belongs to multiple collections; export a specific collection instead");
}

}  // namespace

void runValueSequenceExportTests() {
  exportRequestSequenceLoopsAffectMidiLowering();
  standaloneSequenceExportDoesNotRequireACollection();
}
