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
          .id = PerformanceAutomationId{0},
          .header =
              PerformanceEventHeader{
                  .sourceCommand = CommandId{9},
                  .sourceAnnotation = SourceAnnotationId{11},
                  .track = TrackId{3},
                  .tick = 0,
              },
          .intent =
              ScalarPerformanceAutomationIntent{
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

void performanceEmitterBindsScalarAutomationWithoutExposingStorage() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{9}, SourceAnnotationId{11}, 0, nextSequence, nextNote, nextAutomation};

  const auto fade = out.noteFade(PerformanceAutomationTarget::Pitch, 2.0, 2, 1);
  fade.at(out, 1).pitchBend(1.0);
  fade.at(out, 2).pitchBend(2.0);
  out.note(60, 1.0, 3);

  expect(track.automations.size() == 1 && track.automations[0].points.size() == 2 && track.events.size() == 1,
         "bound output should route only its events into the automation");
  const auto& intent = std::get<ScalarPerformanceAutomationIntent>(track.automations[0].intent);
  expect(intent.target == PerformanceAutomationTarget::Pitch &&
             intent.motion == PerformanceAutomationMotion::TargetOverTicks && intent.targetValue == 2.0 &&
             intent.durationTicks == 2 && intent.delayTicks == 1 && intent.restartsOnNote,
         "emitter automation helpers should construct the declared source intent");
  expect(performanceEventHeader(track.automations[0].points[0]).sourceCommand == CommandId{9} &&
             performanceEventHeader(track.automations[0].points[0]).sourceAnnotation == SourceAnnotationId{11},
         "bound output should retain the automation command's provenance");

  PerformanceTrack otherTrack{.id = TrackId{4}};
  u64 otherSequence = 0;
  u32 otherNote = 0;
  u32 otherAutomation = 0;
  PerformanceEmitter otherOut{otherTrack,    CommandId{10}, SourceAnnotationId{12}, 0,
                              otherSequence, otherNote,     otherAutomation};
  bool rejectedOtherTrack = false;
  try {
    fade.output(otherOut).pitchBend(0.0);
  } catch (const std::logic_error&) {
    rejectedOtherTrack = true;
  }
  expect(rejectedOtherTrack, "an automation binding should not attach to another performance track");
}

void pitchTransitionApiPreservesSamplesAndResolvesLifecyclePolicies() {
  PerformanceTrack track{.id = TrackId{5}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{14}, SourceAnnotationId{16}, 0, nextSequence, nextNote, nextAutomation};

  const PerformanceNoteId retargetNote = out.note(64, 1.0, 16);
  auto first = out.pitchSlide(retargetNote, 60, 64, 8);
  first.preferPitchBend().onNewSlide(AutomationNewAutomationPolicy::Retarget);
  first.sample(out.at(3), 61.5);
  const auto second = out.at(4).pitchSlide(retargetNote, 0, 67, 4);

  const auto& firstAutomation = track.automations[0];
  const auto& firstIntent = std::get<PitchTransitionIntent>(firstAutomation.intent);
  const auto& firstSamples = std::get<SampledAutomationCurve>(firstIntent.curve).samples;
  const auto& secondAutomation = track.automations[1];
  const auto& secondIntent = std::get<PitchTransitionIntent>(secondAutomation.intent);
  expect(firstSamples.size() == 3 && firstSamples[0] == AutomationSample{.tickOffset = 0, .value = 60} &&
             firstSamples[1] == AutomationSample{.tickOffset = 3, .value = 61.5} &&
             firstSamples[2] == AutomationSample{.tickOffset = 8, .value = 64},
         "the compact sample API should retain implicit endpoints and exact source pitch values in tick order");
  expect(firstAutomation.realization.endTick == 4 &&
             firstAutomation.realization.endReason == PerformanceAutomationEndReason::Replaced &&
             secondAutomation.realization.continuesFrom == first.id() && secondIntent.startKey == 61.5,
         "retargeting should clip the previous transition and begin from its realized source curve value");

  const PerformanceNoteId queuedNote = out.at(20).note(69, 1.0, 12);
  auto queuedFirst = out.at(20).pitchSlide(queuedNote, 65, 67, 5);
  queuedFirst.onNewSlide(AutomationNewAutomationPolicy::Queue);
  const auto queuedSecond = out.at(22).pitchSlide(queuedNote, 0, 69, 3);
  const auto& queuedRealization = track.automations[3].realization;
  const auto& queuedIntent = std::get<PitchTransitionIntent>(track.automations[3].intent);
  expect(queuedRealization.requestedStartTick == 22 && queuedRealization.startTick == 25 &&
             queuedRealization.continuesFrom == queuedFirst.id() && queuedIntent.startKey == 67,
         "queueing should delay the successor and chain it from the previous target without format bookkeeping");
  expect(queuedSecond.id().valid(), "a queued transition should retain an opaque automation identity");
  const auto adjacent = out.at(28).pitchSlide(queuedNote, 69, 71, 2);
  expect(track.automations[4].realization.continuesFrom == queuedSecond.id() && adjacent.id().valid(),
         "back-to-back transitions should remain linked even when neither one interrupts the other");

  const PerformanceNoteId interruptedNote = out.at(40).note(72, 1.0, 10);
  const auto interrupted = out.at(40).pitchSlide(interruptedNote, 70, 72, 8);
  const PerformanceNoteId extension = out.at(42).note(72, 1.0, 2, true);
  expect(extension == interruptedNote && track.automations[5].realization.endTick == 48,
         "a source tie should extend one stable note identity without interrupting its transition");
  out.at(43).note(74, 1.0, 2);
  expect(track.automations[5].realization.endTick == 43 &&
             track.automations[5].realization.endReason == PerformanceAutomationEndReason::NewNote &&
             interrupted.id().valid(),
         "a genuinely new note should apply the transition's new-note interruption policy");

  const PerformanceNoteId guardedNote = out.at(50).note(76, 1.0, 10);
  auto guarded = out.at(50).pitchSlide(guardedNote, 74, 76, 6);
  guarded.onNewSlide(AutomationNewAutomationPolicy::Ignore);
  const auto ignored = out.at(52).pitchSlide(guardedNote, 76, 79, 3);
  expect(guarded.id().valid() && !ignored.id().valid() && track.automations[6].realization.endTick == 56,
         "an ignore policy should reject a competing transition without mutating the active one");

  const PerformanceNoteId stoppedNote = out.at(60).note(81, 1.0, 10);
  const auto stopped = out.at(60).pitchSlide(stoppedNote, 79, 81, 6);
  stopped.stop(out.at(62));
  expect(track.automations[7].realization.endTick == 62 &&
             track.automations[7].realization.endReason == PerformanceAutomationEndReason::SourceStopped,
         "formats should be able to stop a transition explicitly through its opaque handle");

  auto configured = out.at(70).pitchSlide(stoppedNote, 81, 84, PitchSlideTiming::fixedDuration(4, 125.0));
  configured.continueFrom(interruptedNote)
      .onNewNote(AutomationNewNotePolicy::Continue)
      .onNewSlide(AutomationNewAutomationPolicy::Queue)
      .onNoteEnd(AutomationNoteEndPolicy::Continue)
      .preferPitchBend()
      .restorePortamentoTiming(250.0)
      .portamentoOverlap(2);
  const auto& configuredIntent = std::get<PitchTransitionIntent>(track.automations[8].intent);
  expect(configuredIntent.previousNote == interruptedNote &&
             configuredIntent.interruptions.newNote == AutomationNewNotePolicy::Continue &&
             configuredIntent.interruptions.newAutomation == AutomationNewAutomationPolicy::Queue &&
             configuredIntent.interruptions.noteEnd == AutomationNoteEndPolicy::Continue &&
             configuredIntent.renderingHint == PitchTransitionRenderingHint::PitchBend &&
             configuredIntent.timing.timelineTicks == 4 &&
             std::get<FixedDurationPitchSlideTiming>(configuredIntent.timing.physical).milliseconds == 125.0 &&
             configuredIntent.nativePortamento && configuredIntent.nativePortamento->restoreTimeMilliseconds == 250.0 &&
             configuredIntent.nativePortamento->overlapTicks == 2,
         "the pitch-slide handle should attach uncommon source behavior without exposing IR construction");
}

}  // namespace

void runValueSequenceModelTests() {
  levelScaleRoundTripsMidiValues();
  byteReaderChecksBoundsAndEndian();
  sourceCommandsRetainOnlySemanticData();
  trackProgramBuilderRejectsDuplicateCommandAddresses();
  collectionIssueHelpersValidateStoredStatus();
  performanceAutomationRetainsIntentAndFlattensExactPointsInExecutionOrder();
  performanceEmitterBindsScalarAutomationWithoutExposingStorage();
  pitchTransitionApiPreservesSamplesAndResolvesLifecyclePolicies();
}
