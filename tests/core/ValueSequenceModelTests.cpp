/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

namespace {

void levelScaleRoundTripsMidiValues() {
  for (u32 value = 0; value <= 127; ++value) {
    const auto midiValue = static_cast<u8>(value);
    expect(LevelScale::midi7FromLinear(LevelScale::linearFromMidi7(midiValue)) == midiValue,
           "MIDI-shaped 7-bit levels should round-trip through linear gain");
  }

  for (u32 value = 0; value <= 16383; ++value) {
    const auto midiValue = static_cast<u16>(value);
    expect(LevelScale::midi14FromLinear(LevelScale::linearFromMidi14(midiValue)) == midiValue,
           "MIDI-shaped 14-bit levels should round-trip through linear gain");
  }
}

void byteReaderChecksBoundsAndEndian() {
  const std::vector<u8> bytes{0x00, 0x34, 0x12, 0x78, 0x56};
  const ByteReader reader(SourceId{7}, bytes);

  expect(reader.has(1, 4), "reader should report valid four-byte range");
  expect(!reader.has(4, 2), "reader should reject range past end");
  expect(reader.u8At(1) == 0x34, "reader should read u8");
  expect(reader.le16(1) == 0x1234, "reader should read little-endian u16");
  expect(reader.be16(1) == 0x3412, "reader should read big-endian u16");
  expect(reader.le32(1) == 0x56781234, "reader should read little-endian u32");
  expect(reader.be32(1) == 0x34127856, "reader should read big-endian u32");

  bool threw = false;
  try {
    static_cast<void>(reader.u8At(5));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  expect(threw, "reader should throw on out-of-range access");
}

void sourceCommandsRetainOnlySemanticData() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{.id = TrackId{0}, .startAddress = Address{0}};
  TrackProgramBuilder builder{track};
  const std::array<u8, 2> programBytes{0x80, 0x05};
  const SourceRange range = probeRange(0, programBytes.size());
  const SourceCommand& command =
      addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, range, programBytes);

  expect(track.commands.size() == 1, "track builder should append one source command");
  expect(command.range == range && command.encodedSize == programBytes.size(),
         "source command should retain the range needed to inspect encoded bytes");
  expect(command.execution.valid(), "source command should retain compiled playback actions");
}

void trackProgramBuilderRejectsDuplicateCommandAddresses() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{.id = TrackId{0}, .startAddress = Address{0}};
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()), programBytes);

  bool rejectedDuplicateAddress = false;
  try {
    static_cast<void>(addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0},
                                                           probeRange(2, programBytes.size()), programBytes));
  } catch (const std::invalid_argument&) {
    rejectedDuplicateAddress = true;
  }
  expect(rejectedDuplicateAddress, "track builder should reject duplicate source command addresses");
  expect(track.commands.size() == 1, "duplicate-address rejection should not mutate the track program");
}

void collectionIssueHelpersValidateStoredStatus() {
  const CollectionIssue missingSequence = missingSequenceIssue();
  expect(missingSequence.severity == Severity::Warning && missingSequence.code == "missing-sequence",
         "missing sequence helper should create a warning issue");
  const std::vector<CollectionIssue> missingIssues{missingSequence};
  expect(validatedCollectionStatus(CollectionStatus::Complete, missingIssues) == CollectionStatus::Incomplete,
         "missing issues should prevent complete collection status");

  const CollectionIssue missingInstrument = missingInstrumentSetIssue(AssetId{7});
  expect(missingInstrument.severity == Severity::Error && missingInstrument.asset == AssetId{7},
         "missing instrument helper should preserve a broken asset reference");

  const CollectionIssue ambiguous = ambiguousMatchIssue("multiple banks match");
  const std::vector<CollectionIssue> ambiguousIssues{ambiguous};
  expect(validatedCollectionStatus(CollectionStatus::Complete, ambiguousIssues) == CollectionStatus::Ambiguous,
         "ambiguous match issue should validate complete status to ambiguous");

  const CollectionIssue removed = removedStaleAssetIssue();
  const std::vector<CollectionIssue> removedIssues{removed};
  expect(validatedCollectionStatus(CollectionStatus::Complete, removedIssues) == CollectionStatus::Stale,
         "removed asset issue should validate complete status to stale");

  DesiredCollection explicitStale{
      .status = CollectionStatus::Stale,
      .issues = {missingSampleCollectionIssue()},
  };
  expect(validatedCollectionStatus(explicitStale) == CollectionStatus::Stale,
         "explicit non-complete status should remain stored instead of being derived from issues");
}

void performanceAutomationRetainsIntentAndFlattensExactPointsInExecutionOrder() {
  const PerformanceTrack track{
      .id = TrackId{3},
      .events =
          {
              NotePerformanceEvent{
                  .header = PerformanceEventHeader{.track = TrackId{3}, .tick = 2, .sequence = 3},
                  .key = 60,
                  .durationTicks = 1,
              },
              MarkerPerformanceEvent{
                  .header = PerformanceEventHeader{.track = TrackId{3}, .tick = 0, .sequence = 0},
                  .text = "start",
              },
          },
      .automations = {PerformanceAutomation{
          .header =
              PerformanceEventHeader{
                  .sourceCommand = CommandId{9},
                  .sourceAnnotation = SourceAnnotationId{11},
                  .track = TrackId{3},
                  .tick = 0,
              },
          .intent =
              PerformanceAutomationIntent{
                  .target = PerformanceAutomationTarget::Level,
                  .targetValue = 0.5,
                  .durationTicks = 1,
              },
          .points =
              {
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{
                              .sourceCommand = CommandId{9},
                              .sourceAnnotation = SourceAnnotationId{11},
                              .track = TrackId{3},
                              .tick = 1,
                              .sequence = 2,
                          },
                      .linearGain = 0.5,
                  },
                  LevelPerformanceEvent{
                      .header =
                          PerformanceEventHeader{
                              .sourceCommand = CommandId{9},
                              .sourceAnnotation = SourceAnnotationId{11},
                              .track = TrackId{3},
                              .tick = 0,
                              .sequence = 1,
                          },
                      .linearGain = 0.75,
                  },
              },
      }},
  };

  const auto flattened = flattenedPerformanceEvents(track);
  expect(flattened.size() == 4 && performanceEventHeader(*flattened[0]).sequence == 0 &&
             performanceEventHeader(*flattened[1]).sequence == 1 &&
             performanceEventHeader(*flattened[2]).sequence == 2 && performanceEventHeader(*flattened[3]).sequence == 3,
         "flattened performance events should merge ordinary events and exact automation points by tick and order");

  const auto sourceEvents = performanceEventsForCommand(track, CommandId{9});
  expect(sourceEvents.size() == 2 && std::ranges::all_of(sourceEvents,
                                                         [](const PerformanceEvent* event) {
                                                           return performanceEventHeader(*event).sourceAnnotation ==
                                                                  SourceAnnotationId{11};
                                                         }),
         "automation points should remain linked to the command and annotation that defined their intent");
}

void performanceEmitterBindsAutomationWithoutExposingStorage() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  PerformanceEmitter out{track, CommandId{9}, SourceAnnotationId{11}, 0, nextSequence};

  const auto fade = out.noteFade(PerformanceAutomationTarget::Pitch, 2.0, 2, 1);
  fade.at(out, 1).pitchBend(1.0);
  fade.at(out, 2).pitchBend(2.0);
  out.note(60, 1.0, 3);

  expect(track.automations.size() == 1 && track.automations[0].points.size() == 2 && track.events.size() == 1,
         "bound output should route only its events into the automation");
  const auto& automation = track.automations[0];
  expect(automation.intent.target == PerformanceAutomationTarget::Pitch &&
             automation.intent.motion == PerformanceAutomationMotion::TargetOverTicks &&
             automation.intent.targetValue == 2.0 && automation.intent.durationTicks == 2 &&
             automation.intent.delayTicks == 1 && automation.intent.restartsOnNote,
         "emitter automation helpers should construct the declared source intent");
  expect(performanceEventHeader(automation.points[0]).sourceCommand == CommandId{9} &&
             performanceEventHeader(automation.points[0]).sourceAnnotation == SourceAnnotationId{11},
         "bound output should retain the automation command's provenance");

  PerformanceTrack otherTrack{.id = TrackId{4}};
  u64 otherSequence = 0;
  PerformanceEmitter otherOut{otherTrack, CommandId{10}, SourceAnnotationId{12}, 0, otherSequence};
  bool rejectedOtherTrack = false;
  try {
    fade.output(otherOut).pitchBend(0.0);
  } catch (const std::logic_error&) {
    rejectedOtherTrack = true;
  }
  expect(rejectedOtherTrack, "an automation binding should not attach to another performance track");
}

void stepDrivenAutomationUsesExactPointsAsItsNeutralRate() {
  PerformanceTrack track{.id = TrackId{5}};
  u64 nextSequence = 0;
  PerformanceEmitter out{track, CommandId{14}, SourceAnnotationId{16}, 0, nextSequence};

  const auto step = out.step(PerformanceAutomationTarget::Level, 0.25);
  step.at(out, 1).level(0.75);
  step.at(out, 2).level(0.5);

  const auto& automation = track.automations.front();
  expect(automation.intent.motion == PerformanceAutomationMotion::TargetByStep &&
             automation.intent.durationTicks == 0 && automation.points.size() == 2,
         "step-driven automation should remain open-ended while retaining its exact realized rate");
  expect(std::get<LevelPerformanceEvent>(automation.points[0]).linearGain == 0.75 &&
             std::get<LevelPerformanceEvent>(automation.points[1]).linearGain == 0.5,
         "step-driven automation points should preserve neutral values after source conversion");
}

}  // namespace

void runValueSequenceModelTests() {
  levelScaleRoundTripsMidiValues();
  byteReaderChecksBoundsAndEndian();
  sourceCommandsRetainOnlySemanticData();
  trackProgramBuilderRejectsDuplicateCommandAddresses();
  collectionIssueHelpersValidateStoredStatus();
  performanceAutomationRetainsIntentAndFlattensExactPointsInExecutionOrder();
  performanceEmitterBindsAutomationWithoutExposingStorage();
  stepDrivenAutomationUsesExactPointsAsItsNeutralRate();
}
