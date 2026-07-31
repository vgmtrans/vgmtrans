/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"
#include "value/sequence/SequenceMotion.h"

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

void collectionIssuesDeriveResolutionIndependentlyFromFreshness() {
  const CollectionIssue missingSequence = missingSequenceIssue();
  expect(missingSequence.impact == CollectionIssueImpact::Incomplete && missingSequence.severity == Severity::Warning &&
             missingSequence.code == "missing-sequence",
         "missing sequence helper should create a warning issue");
  const std::vector<CollectionIssue> missingIssues{missingSequence};
  expect(collectionResolution(missingIssues) == CollectionResolution::Incomplete,
         "missing issues should make collection resolution incomplete");

  const CollectionIssue missingInstrument = missingInstrumentSetIssue(AssetId{7});
  expect(missingInstrument.severity == Severity::Error && missingInstrument.asset == AssetId{7},
         "missing instrument helper should preserve a broken asset reference");

  const CollectionIssue ambiguous = ambiguousMatchIssue("multiple banks match");
  const std::vector<CollectionIssue> ambiguousIssues{ambiguous};
  expect(collectionResolution(ambiguousIssues) == CollectionResolution::Ambiguous,
         "ambiguous match issue should make collection resolution ambiguous");
  expect(collectionResolution(std::vector{missingSequence, ambiguous}) == CollectionResolution::Ambiguous,
         "ambiguity should take precedence when a collection is also incomplete");

  const CollectionIssue removed = removedStaleAssetIssue();
  Collection stale{
      .freshness = CollectionFreshness::Stale,
      .issues = {missingSampleCollectionIssue()},
  };
  expect(removed.impact == CollectionIssueImpact::Incomplete, "removed asset issue should make resolution incomplete");
  expect(stale.freshness == CollectionFreshness::Stale && stale.resolution() == CollectionResolution::Incomplete,
         "freshness and resolution should remain independent collection properties");
}

void performanceAutomationRetainsIntentAlongsideOneEventTimeline() {
  const PerformanceTrack track{
      .id = TrackId{3},
      .events =
          {
              MarkerPerformanceEvent{
                  .header = PerformanceEventHeader{.track = TrackId{3}, .tick = 0, .sequence = 0},
                  .text = "start",
              },
              LevelPerformanceEvent{
                  .header =
                      PerformanceEventHeader{
                          .sourceCommand = CommandId{9},
                          .sourceAnnotation = SourceAnnotationId{11},
                          .track = TrackId{3},
                          .tick = 0,
                          .sequence = 1,
                          .automation = PerformanceAutomationId{0},
                      },
                  .linearGain = 0.75,
              },
              LevelPerformanceEvent{
                  .header =
                      PerformanceEventHeader{
                          .sourceCommand = CommandId{9},
                          .sourceAnnotation = SourceAnnotationId{11},
                          .track = TrackId{3},
                          .tick = 1,
                          .sequence = 2,
                          .automation = PerformanceAutomationId{0},
                      },
                  .linearGain = 0.5,
              },
              NotePerformanceEvent{
                  .header = PerformanceEventHeader{.track = TrackId{3}, .tick = 2, .sequence = 3},
                  .key = 60,
                  .durationTicks = 1,
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
      }},
  };

  expect(track.events.size() == 4 && performanceEventHeader(track.events[1]).automation == PerformanceAutomationId{0} &&
             performanceEventHeader(track.events[2]).automation == PerformanceAutomationId{0},
         "realized automation events should remain in the track timeline with their source-intent association");

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

  expect(track.automations.size() == 1 && track.events.size() == 3 &&
             performanceEventHeader(track.events[0]).automation == track.automations[0].id &&
             performanceEventHeader(track.events[1]).automation == track.automations[0].id &&
             !performanceEventHeader(track.events[2]).automation,
         "bound output should tag its realized events without removing them from the track timeline");
  const auto& intent = std::get<ScalarPerformanceAutomationIntent>(track.automations[0].intent);
  expect(intent.target == PerformanceAutomationTarget::Pitch &&
             intent.motion == PerformanceAutomationMotion::TargetOverTicks && intent.targetValue == 2.0 &&
             intent.durationTicks == 2 && intent.delayTicks == 1 && intent.restartsOnNote,
         "emitter automation helpers should construct the declared source intent");
  expect(performanceEventHeader(track.events[0]).sourceCommand == CommandId{9} &&
             performanceEventHeader(track.events[0]).sourceAnnotation == SourceAnnotationId{11},
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

void performanceBoundValueOwnsReplacementLifecycle() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{9}, SourceAnnotationId{11}, 0, nextSequence, nextNote, nextAutomation};
  PerformanceBoundValue<SequenceAutomatedValue<double>> value;
  value.reset(0.0);

  static_cast<void>(
      value.begin(out.fade(PerformanceAutomationTarget::Level, 1.0, 8),
                  SequenceMotionPlan<double>::targetOverTicks(1.0, 8)));
  static_cast<void>(
      value.begin(out.at(3).fade(PerformanceAutomationTarget::Level, 0.5, 4),
                  SequenceMotionPlan<double>::targetOverTicks(0.5, 4)));
  value.setCurrentAt(5, 0.25);

  expect(track.automations.size() == 2 && track.automations[0].realization.endTick == 3 &&
             track.automations[0].realization.endReason == PerformanceAutomationEndReason::Interrupted &&
             track.automations[1].realization.endTick == 5 &&
             track.automations[1].realization.endReason == PerformanceAutomationEndReason::Interrupted &&
             value.current() == 0.25,
         "bound values should end automation when a new motion or immediate value takes over");
}

void performanceEmitterResolvesDeclaredPanLawIntoEvents() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,        CommandId{9}, SourceAnnotationId{11}, 0,
                         nextSequence, nextNote,     nextAutomation,         PanLaw::ConstantSum};

  out.pan(0.0);
  expect(std::get<PanPerformanceEvent>(track.events.front()).law == PanLaw::ConstantSum,
         "positional pan events should retain the program's resolved pan law");

  PerformanceTrack undeclaredTrack{.id = TrackId{4}};
  PerformanceEmitter undeclared{undeclaredTrack, CommandId{10}, SourceAnnotationId{12}, 0,
                                nextSequence,    nextNote,      nextAutomation};
  bool rejectedUndeclaredPan = false;
  try {
    undeclared.pan(0.0);
  } catch (const std::logic_error&) {
    rejectedUndeclaredPan = true;
  }
  expect(rejectedUndeclaredPan, "positional pan should reject a format that did not declare its pan law");
}

void pitchTransitionApiPreservesSamplesAndRealizedLifecycle() {
  PerformanceTrack track{.id = TrackId{5}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track, CommandId{14}, SourceAnnotationId{16}, 0, nextSequence, nextNote, nextAutomation};

  const PerformanceNoteId retargetNote = out.note(64, 1.0, 16);
  auto first = out.pitchSlide(retargetNote, 60, 64, 8);
  first.sample(out.at(3), 61.5);
  out.at(4).retargetPitchSlide(retargetNote, 64, 67, 4);

  const auto& firstAutomation = track.automations[0];
  const auto& firstIntent = std::get<PitchTransitionIntent>(firstAutomation.intent);
  const auto& firstSamples = std::get<SampledAutomationCurve>(firstIntent.curve).samples;
  expect(firstSamples.size() == 3 && firstSamples[0] == AutomationSample{.tickOffset = 0, .value = 60} &&
             firstSamples[1] == AutomationSample{.tickOffset = 3, .value = 61.5} &&
             firstSamples[2] == AutomationSample{.tickOffset = 8, .value = 64},
         "the compact sample API should retain implicit endpoints and exact source pitch values in tick order");
  expect(firstAutomation.realization.endTick == 4 &&
             firstAutomation.realization.endReason == PerformanceAutomationEndReason::Continued &&
             std::get<PitchTransitionIntent>(track.automations[1].intent).startKey == 61.5,
         "a realized replacement should clip the previous transition and retain the source driver's current pitch");

  const PerformanceNoteId chainedNote = out.at(20).note(69, 1.0, 12);
  out.at(20).retargetPitchSlide(chainedNote, 65, 67, 5);
  out.at(25).retargetPitchSlide(chainedNote, 65, 69, 3);
  out.at(28).retargetPitchSlide(chainedNote, 65, 71, 2);
  expect(track.automations[2].realization.endReason == PerformanceAutomationEndReason::Continued &&
             track.automations[3].realization.endReason == PerformanceAutomationEndReason::Continued &&
             track.automations[3].realization.startTick == 25,
         "playback code should emit queued source motion at its realized tick and retain adjacent chaining");

  const PerformanceNoteId interruptedNote = out.at(40).note(72, 1.0, 10);
  out.at(40).pitchSlide(interruptedNote, 70, 72, 8);
  const PerformanceNoteId extension = out.at(42).note(72, 1.0, 2, true);
  expect(extension == interruptedNote && track.automations[5].realization.endTick == 48,
         "a source tie should extend one stable note identity without interrupting its transition");
  out.at(43).note(74, 1.0, 2);
  expect(track.automations[5].realization.endTick == 43 &&
             track.automations[5].realization.endReason == PerformanceAutomationEndReason::Interrupted,
         "a genuinely new note should mark the transition interrupted at that tick");

  const PerformanceNoteId replacedNote = out.at(50).note(76, 1.0, 10);
  out.at(50).pitchSlide(replacedNote, 74, 76, 6);
  out.at(52).retargetPitchSlide(replacedNote, 76, 79, 3);
  expect(track.automations[6].realization.endTick == 52 &&
             track.automations[6].realization.endReason == PerformanceAutomationEndReason::Continued &&
             std::abs(std::get<PitchTransitionIntent>(track.automations[7].intent).startKey - (74.0 + 2.0 / 3.0)) <
                 0.0001,
         "retargeting should replace active motion from its shared linear value");

  const PerformanceNoteId stoppedNote = out.at(60).note(81, 1.0, 10);
  const auto stopped = out.at(60).pitchSlide(stoppedNote, 79, 81, 6);
  stopped.stop(out.at(62));
  expect(track.automations[8].realization.endTick == 62 &&
             track.automations[8].realization.endReason == PerformanceAutomationEndReason::Interrupted,
         "formats should be able to stop a transition explicitly through its opaque handle");

  auto configured = out.at(70).pitchSlide(stoppedNote, 81, 84, PitchSlideTiming::fixedDuration(4, 125.0));
  configured.continueFrom(interruptedNote)
      .continueAcrossNotes()
      .preferPitchBend()
      .restorePortamentoTiming(250.0)
      .portamentoOverlap(2);
  const auto& configuredIntent = std::get<PitchTransitionIntent>(track.automations[9].intent);
  expect(configuredIntent.previousNote == interruptedNote && configuredIntent.continuesAcrossNotes &&
             configuredIntent.preferredRendering == PitchTransitionRenderingHint::PitchBend &&
             configuredIntent.timing.timelineTicks == 4 &&
             std::get<FixedDurationPitchSlideTiming>(configuredIntent.timing.physical).milliseconds == 125.0 &&
             configuredIntent.nativePortamento.restoreTimeMilliseconds == 250.0 &&
             configuredIntent.nativePortamento.overlapTicks == 2,
         "the pitch-slide handle should attach uncommon source behavior without exposing IR construction");
}

}  // namespace

void runValueSequenceModelTests() {
  levelScaleRoundTripsMidiValues();
  byteReaderChecksBoundsAndEndian();
  sourceCommandsRetainOnlySemanticData();
  trackProgramBuilderRejectsDuplicateCommandAddresses();
  collectionIssuesDeriveResolutionIndependentlyFromFreshness();
  performanceAutomationRetainsIntentAlongsideOneEventTimeline();
  performanceEmitterBindsScalarAutomationWithoutExposingStorage();
  performanceBoundValueOwnsReplacementLifecycle();
  performanceEmitterResolvesDeclaredPanLawIntoEvents();
  pitchTransitionApiPreservesSamplesAndRealizedLifecycle();
}
