/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"

#include "value/base/LevelScale.h"
#include "value/base/Source.h"
#include "value/sequence/SequenceMotion.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/TempoRelativeModulation.h"
#include "value/validation/SequenceValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace vgmtrans::core;

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
  expect(reader.le24(2) == 0x567812 && reader.be24(2) == 0x127856,
         "24-bit reads must accept exactly three available bytes and preserve byte order");
  expect(reader.le32(1) == 0x56781234, "reader should read little-endian u32");
  expect(reader.be32(1) == 0x34127856, "reader should read big-endian u32");

  expectThrows<std::out_of_range>([&] { static_cast<void>(reader.u8At(5)); },
                                  "reader should throw on out-of-range access");

  const std::array<u8, 3> maximumBytes{0xff, 0xff, 0xff};
  const ByteReader maximum{SourceId{7}, maximumBytes};
  for (const auto read : {&ByteReader::le24, &ByteReader::be24}) {
    expect((maximum.*read)(0) == 0xffffff, "24-bit reads must preserve every bit without sign extension");
    for (const u64 offset : {u64{3}, u64{5}, std::numeric_limits<u64>::max()}) {
      expectThrows<std::out_of_range>([&] { static_cast<void>((reader.*read)(offset)); },
                                      "24-bit reads must reject truncated and overflowing source offsets");
    }
  }
}

void sequenceSourceRangeIncludesDecodedCommandsFromTheBaseSource() {
  const std::vector<u8> bytes(32);
  const ByteReader reader(SourceId{7}, bytes);
  const SourceRange baseRange = reader.range(10, 4);
  const SequenceProgram program{
      .tracks =
          {
              TrackProgram{
                  .commands =
                      {
                          SourceCommand{.range = reader.range(4, 2)},
                          SourceCommand{.range = reader.range(20, 3)},
                          SourceCommand{.range = SourceRange{.source = SourceId{8}, .offset = 0, .size = 30}},
                      },
              },
          },
  };

  expect(sequenceSourceRange(reader, baseRange, program) == reader.range(4, 19),
         "sequence source range should span its base range and same-source decoded commands only");
  expect(sequenceSourceRange(reader, {}, program) == reader.range(4, 19),
         "a headerless sequence should infer its range from commands in the input source");
}

void sequenceValidationProtectsPositionalCommandStorage() {
  const SequenceProgram program{
      .tracks =
          {
              TrackProgram{
                  .startAddress = Address{2},
                  .commands =
                      {
                          SourceCommand{.address = Address{2}},
                          SourceCommand{.address = Address{2}},
                          SourceCommand{.address = Address{1}},
                      },
              },
              TrackProgram{
                  .startAddress = Address{0},
                  .commands = {SourceCommand{.address = Address{1}}},
              },
          },
  };

  const ValidationReport validation = validateSequenceProgram(program);
  const auto diagnostics = validation.diagnostics();
  expect(std::ranges::count(diagnostics, "sequence.track.command-order", &Diagnostic::code) == 2 &&
             std::ranges::count(diagnostics, "sequence.track.missing-start", &Diagnostic::code) == 1,
         "sequence validation should reject duplicate, out-of-order, and missing-start command storage");
}

void performanceEventsForCommandMatchBothTrackAndCommand() {
  const PerformanceTrack track{
      .id = TrackId{3},
      .events =
          {
              MarkerPerformanceEvent{.text = "unowned"},
              LevelPerformanceEvent{.header = {.sourceCommand = {TrackId{3}, CommandId{9}}}, .linearGain = 0.75},
              LevelPerformanceEvent{.header = {.sourceCommand = {TrackId{3}, CommandId{9}}}, .linearGain = 0.5},
              NotePerformanceEvent{.header = {.sourceCommand = {TrackId{4}, CommandId{9}}}, .key = 60},
              NotePerformanceEvent{.header = {.sourceCommand = {TrackId{3}, CommandId{10}}}, .key = 62},
          },
  };

  expect(performanceEventsForCommand(track, {TrackId{3}, CommandId{9}}) ==
             std::vector<const PerformanceEvent*>{&track.events[1], &track.events[2]},
         "command lookup should return only matching events in timeline order");
}

void performanceEmitterBindsScalarAutomationWithoutExposingStorage() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{9}}, SourceAnnotationId{11}, 0, nextSequence, nextNote,
                         nextAutomation};

  const auto fade = out.fade(PerformanceAutomationTarget::Pitch, 2.0, 2, 1);
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
             intent.durationTicks == 2 && intent.delayTicks == 1,
         "emitter automation helpers should construct the declared source intent");
  expect(performanceEventHeader(track.events[0]).sourceCommand.id == CommandId{9} &&
             performanceEventHeader(track.events[0]).sourceAnnotation == SourceAnnotationId{11},
         "bound output should retain the automation command's provenance");

  PerformanceTrack otherTrack{.id = TrackId{4}};
  u64 otherSequence = 0;
  u32 otherNote = 0;
  u32 otherAutomation = 0;
  PerformanceEmitter otherOut{
      otherTrack, {otherTrack.id, CommandId{10}}, SourceAnnotationId{12}, 0, otherSequence, otherNote, otherAutomation};
  expectThrows<std::logic_error>([&] { fade.output(otherOut).pitchBend(0.0); },
                                 "an automation binding should not attach to another performance track");
}

void sequenceMotionPreservesDelayAndTargetCompletion() {
  SequenceLinearMotion<s32> motion;
  expect(motion.begin(SequenceMotionPlan<s32>::targetOverTicks(10, 3, 1)).status == SequenceMotionStatus::Delayed,
         "a delayed fade must start without advancing the value");
  expect(motion.tick().status == SequenceMotionStatus::Delayed && motion.current() == 0,
         "the delay must expire before the first arithmetic step");
  expect(motion.tick().current == 3 && motion.tick().current == 6,
         "integer fades must retain truncation of each computed step");
  expect(motion.tick().status == SequenceMotionStatus::Finished && motion.current() == 10 && !motion.active(),
         "the last timed tick must reach the target exactly despite step truncation");

  motion.reset(0);
  motion.begin(SequenceMotionPlan<s32>::targetOverTicksWithStep(10, 1, 2));
  expect(motion.tick().current == 1 && motion.tick().current == 10,
         "a supplied timed step must be preserved until the final target snap");
  motion.begin(SequenceMotionPlan<s32>::targetByStep(0, -4));
  expect(motion.tick().current == 6 && motion.tick().current == 2 && motion.tick().current == 0 && !motion.active(),
         "step-based motion must stop when it crosses the target");
  expect(motion.begin(SequenceMotionPlan<s32>::targetOverTicks(7, 0, 5)).status == SequenceMotionStatus::Finished &&
             motion.current() == 7 && !motion.active(),
         "zero-duration fades must finish immediately even when a delay was requested");
  expect(motion.begin(SequenceMotionPlan<s32>::targetByStep(2, 0, 5)).status == SequenceMotionStatus::Finished &&
             motion.current() == 2 && !motion.active(),
         "zero-step motion must finish immediately instead of remaining active forever");
}

void fixedPointMotionRetargetsFromTheRoundedSourceValue() {
  for (const auto rounding : {SequenceFixedPointRounding::Floor, SequenceFixedPointRounding::TowardZero,
                              SequenceFixedPointRounding::Nearest}) {
    SequenceFixedPointAutomation<> motion;
    motion.setRounding(rounding);
    motion.begin(motion.toRawTarget(-5, 2));
    expect(motion.tick().current == -640, "fixed-point motion must retain fractional steps internally");
    const s32 raw = rounding == SequenceFixedPointRounding::TowardZero ? -2 : -3;
    expect(motion.currentRaw() == raw, "negative raw values must obey the driver's selected rounding policy");
    motion.begin(motion.toRawTarget(0, 2));
    expect(motion.currentFixed() == raw * 256 && motion.tick().current == raw * 128,
           "retargeting must discard the old fractional accumulator before computing the next step");
    expect(motion.tick().status == SequenceMotionStatus::Finished && motion.currentRaw() == 0,
           "a retargeted fixed-point fade must still finish on its declared target tick");

    motion.begin(motion.toRawTargetByFixedStep(-1, -96, 1));
    expect(motion.tick().status == SequenceMotionStatus::Delayed && motion.tick().current == -96 &&
               motion.tick().current == -192,
           "a raw target must retain the driver's fixed-point step and delay without rescaling them");
    expect(motion.tick().status == SequenceMotionStatus::Finished && motion.currentFixed() == -256,
           "fixed-step motion must stop at the scaled raw target");

    motion.begin(SequenceMotionPlan<s32>::targetOverTicks(std::numeric_limits<s32>::min(), 0));
    expect(motion.currentRaw() == -8'388'608, "rounding must preserve the lowest fixed-point value without negating it");
    motion.begin(SequenceMotionPlan<s32>::targetOverTicks(std::numeric_limits<s32>::max(), 0));
    expect(motion.currentRaw() == (rounding == SequenceFixedPointRounding::Nearest ? 8'388'608 : 8'388'607),
           "rounding up must not overflow the fixed-point accumulator");
  }
}

void performanceBoundValueOwnsReplacementLifecycle() {
  PerformanceTrack track{.id = TrackId{3}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{9}}, SourceAnnotationId{11}, 0, nextSequence, nextNote,
                         nextAutomation};
  PerformanceBoundValue<SequenceLinearMotion<double>> value;
  value.reset(0.0);

  value.begin(out, PerformanceAutomationTarget::Level, 1.0,
              SequenceMotionPlan<double>::targetOverTicks(128.0, 8, 1));
  const auto& intent = std::get<ScalarPerformanceAutomationIntent>(track.automations[0].intent);
  expect(intent.motion == PerformanceAutomationMotion::TargetOverTicks && intent.targetValue == 1.0 &&
             intent.durationTicks == 8 && intent.delayTicks == 1 &&
             track.automations[0].realization.startTick == 1 && track.automations[0].realization.endTick == 9,
         "bound motion should retain its timing and converted target in the performance intent");
  value.begin(out.at(3), PerformanceAutomationTarget::Level, 0.5,
              SequenceMotionPlan<double>::targetOverTicks(0.5, 4));
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
  PerformanceEmitter out{track,    {track.id, CommandId{9}}, SourceAnnotationId{11}, 0, nextSequence,
                         nextNote, nextAutomation,           PanLaw::ConstantSum};

  out.pan(0.0);
  expect(std::get<PanPerformanceEvent>(track.events.front()).law == PanLaw::ConstantSum,
         "positional pan events should retain the program's resolved pan law");

  PerformanceTrack undeclaredTrack{.id = TrackId{4}};
  PerformanceEmitter undeclared{
      undeclaredTrack, {undeclaredTrack.id, CommandId{10}}, SourceAnnotationId{12}, 0, nextSequence, nextNote,
      nextAutomation};
  expectThrows<std::logic_error>([&] { undeclared.pan(0.0); },
                                 "positional pan should reject a format that did not declare its pan law");
}

void pitchTransitionApiPreservesSamplesAndRealizedLifecycle() {
  PerformanceTrack track{.id = TrackId{5}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{14}}, SourceAnnotationId{16}, 0, nextSequence, nextNote,
                         nextAutomation};

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
             track.automations[3].realization.startTick == 25 &&
             std::get<PitchTransitionIntent>(track.automations[3].intent).startKey == 67 &&
             std::get<PitchTransitionIntent>(track.automations[4].intent).startKey == 69,
         "playback code should emit queued source motion at its realized tick and retain adjacent chaining");

  const PerformanceNoteId interruptedNote = out.at(40).note(72, 1.0, 10);
  out.at(40).pitchSlide(interruptedNote, 70, 72, 8);
  const PerformanceNoteId extension = out.at(42).note(72, 1.0, 2, true);
  expect(extension == interruptedNote && track.automations[5].realization.endTick == 48,
         "a source tie should extend one stable note identity without interrupting its transition");
  out.at(43).note(74, 1.0, 2);
  expect(track.automations[5].realization.endTick == 43 &&
             track.automations[5].realization.endReason == PerformanceAutomationEndReason::Interrupted,
         "a new note should interrupt the transition at that tick");

  const PerformanceNoteId replacedNote = out.at(50).note(76, 1.0, 10);
  out.at(50).pitchSlide(replacedNote, 74, 76, 6);
  out.at(52).retargetPitchSlide(replacedNote, 76, 79, 3);
  expect(
      track.automations[6].realization.endTick == 52 &&
          track.automations[6].realization.endReason == PerformanceAutomationEndReason::Continued &&
          std::abs(std::get<PitchTransitionIntent>(track.automations[7].intent).startKey - (74.0 + 2.0 / 3.0)) < 0.0001,
      "replacement motion should start from the prior transition's shared linear value");

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
             configuredIntent.portamentoRendering.restoreTimeMilliseconds == 250.0 &&
             configuredIntent.portamentoRendering.overlapTicks == 2,
         "the pitch-slide handle should attach uncommon source behavior without exposing IR construction");
}

void continuedVoiceResolvesPriorPitchMotion() {
  PerformanceTrack track{.id = TrackId{6}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{15}}, SourceAnnotationId{17}, 0, nextSequence, nextNote,
                         nextAutomation};

  const PerformanceNoteId first = out.note(60, 1.0, 8);
  out.pitchSlide(first, 60, 62, 4);
  const PerformanceNoteId samePitch =
      out.at(4).continueVoice(first, NotePerformanceEvent{.key = 62, .linearVelocity = 1.0, .durationTicks = 4});
  expect(samePitch == first && std::get<NotePerformanceEvent>(track.events.back()).extendsPrevious,
         "continuing at a completed slide target should extend the existing note identity");

  const PerformanceNoteId changedPitch =
      out.at(8).continueVoice(samePitch, NotePerformanceEvent{.key = 64, .linearVelocity = 1.0, .durationTicks = 4});
  const auto& transition = std::get<PitchTransitionIntent>(track.automations.back().intent);
  expect(changedPitch != samePitch && transition.previousNote == samePitch && transition.startKey == 62 &&
             transition.targetKey == 64 && transition.timing.timelineTicks == 0,
         "a continued voice that changes key should emit one attack-free boundary transition");
}

void previousNoteEndRetainsContinuationChainBehavior() {
  PerformanceTrack track{.id = TrackId{7}};
  u64 nextSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  PerformanceEmitter out{track,         {track.id, CommandId{18}}, SourceAnnotationId{19}, 0, nextSequence, nextNote,
                         nextAutomation};

  expect(!out.setPreviousNoteEnd(4), "revising the previous note should still report failure when none exists");
  const PerformanceNoteId unrelated = out.note(55, 1.0, 6);
  const PerformanceNoteId base = out.at(10).note(60, 1.0, 20);
  out.at(12).level(0.5);
  const PerformanceNoteId extension = out.at(18).note(60, 0.75, 4, true);

  expect(unrelated != base && extension == base && out.at(20).setPreviousNoteEnd(25),
         "the previous-note convenience API should resolve the latest continuation's stable identity");
  const auto& unrelatedEvent = std::get<NotePerformanceEvent>(track.events[0]);
  const auto& baseEvent = std::get<NotePerformanceEvent>(track.events[1]);
  const auto& extensionEvent = std::get<NotePerformanceEvent>(track.events[3]);
  expect(unrelatedEvent.durationTicks == 6 && baseEvent.durationTicks == 15 && extensionEvent.durationTicks == 7,
         "revising a continuation chain should retain the former durations and stopping boundary exactly");
}

void tempoMapPreservesOrderingAndBoundsDurationConversion() {
  const PerformanceSequence performance{
      .timebase = {.ppqn = 100},
      .tracks =
          {
              PerformanceTrack{.events =
                                   {
                                       TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 12, .sequence = 2},
                                                             .microsecondsPerQuarter = 1000000},
                                       TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 12, .sequence = 1},
                                                             .microsecondsPerQuarter = 750000},
                                       TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 24, .sequence = 4},
                                                             .microsecondsPerQuarter = 250000},
                                       TempoPerformanceEvent{.header = {.track = TrackId{0}, .tick = 30, .sequence = 5},
                                                             .microsecondsPerQuarter = 250000},
                                   }},
              PerformanceTrack{.events =
                                   {
                                       TempoPerformanceEvent{.header = {.track = TrackId{1}, .tick = 12, .sequence = 2},
                                                             .microsecondsPerQuarter = 1250000},
                                   }},
          },
  };
  const PerformanceTempoMap tempos{performance};
  expect(tempos.microsecondsPerQuarterAt(0) == 500000 && tempos.microsecondsPerQuarterAt(12) == 1250000 &&
             tempos.microsecondsPerQuarterAt(24) == 250000 && tempos.points().size() == 4 &&
             tempos.points().back().tick == 24,
         "tempo order must remain stable for equal tick/sequence pairs while repeated writes are omitted");
  expect(tempos.durationMilliseconds(0, 24) == 210.0 && tempos.durationTicksForMilliseconds(0, 210.0) == 24 &&
             tempos.durationTicksForMilliseconds(6, 180.0) == 18,
         "physical durations must include each tempo segment and preserve exact change boundaries");

  const PerformanceTempoMap oneMillisecondTicks{
      PerformanceSequence{.timebase = {.ppqn = 1000}, .initialTempoMicrosecondsPerQuarter = 1000000}};
  constexpr u32 maximum = std::numeric_limits<u32>::max();
  expect(oneMillisecondTicks.durationTicksForMilliseconds(0, 1.5) == 1 &&
             oneMillisecondTicks.durationTicksForMilliseconds(0, 1.501) == 2 &&
             oneMillisecondTicks.durationTicksForMilliseconds(0, maximum - 0.5) == maximum - 1 &&
             oneMillisecondTicks.durationTicksForMilliseconds(0, maximum - 0.25) == maximum,
         "physical duration conversion must retain half-down rounding at ordinary and maximum tick counts");
  expect(oneMillisecondTicks.durationTicksForMilliseconds(0, std::numeric_limits<double>::max()) == maximum &&
             tempos.durationTicksForMilliseconds(6, std::numeric_limits<double>::max()) == maximum &&
             tempos.durationTicksForMilliseconds(0, -1.0) == 0 &&
             tempos.durationTicksForMilliseconds(0, std::numeric_limits<double>::infinity()) == 0 &&
             tempos.durationTicksForMilliseconds(0, std::numeric_limits<double>::quiet_NaN()) == 0,
         "finite durations must saturate before integer conversion, while invalid durations produce zero ticks");
}

void physicalTimingUsesTheFullInternalDivision() {
  for (u32 ppqn : {100u, 65536u, 131072u, 1u << 30}) {
    PerformanceSequence performance{
        .timebase = {.ppqn = ppqn, .midiPpqn = 48},
        .tracks = {PerformanceTrack{
            .events =
                {
                    ModulationPerformanceEvent{.target = ModulationPerformanceTarget::VibratoRate,
                                               .context =
                                                   {
                                                       .cyclesPerTick = 2.0 / ppqn,
                                                       .delay = LfoDelay{.ticks = ppqn / 4, .tempoRelative = true},
                                                   }},
                    TempoPerformanceEvent{.header = {.tick = ppqn / 2}, .microsecondsPerQuarter = 1000000},
                }}},
    };
    const PerformanceTempoMap tempos{performance};
    expect(std::abs(tempos.tickSeconds(0) * ppqn - 0.5) < 1e-12 &&
               std::abs(tempos.tickSeconds(ppqn / 2) * ppqn - 1.0) < 1e-12,
           "physical tick duration must use the full internal division independently of MIDI division");
    expect(tempos.durationMilliseconds(0, ppqn) == 750.0 && tempos.durationMilliseconds(ppqn / 4, ppqn / 2) == 375.0 &&
               tempos.durationTicksForMilliseconds(0, 750.0) == ppqn &&
               tempos.durationTicksForMilliseconds(ppqn / 4, 375.0) == ppqn / 2,
           "duration conversion must preserve quarter-note timing across tempo changes at large divisions");

    resolveTempoRelativeModulation(performance);
    const auto rates = orderedPerformanceEvents<ModulationPerformanceEvent>(performance);
    expect(rates.size() == 2 && rates[0]->context.frequencyHz == 4.0 && rates[0]->context.delay &&
               rates[0]->context.delay->milliseconds == 125.0 && rates[1]->context.frequencyHz == 2.0 &&
               rates[1]->context.delay && rates[1]->context.delay->milliseconds == 250.0,
           "tempo-relative modulation must preserve physical rates and delays at large internal divisions");
  }
}

void tempoMapRetainsInitialTempoAndOwnsItsPoints() {
  PerformanceSequence performance{
      .initialTempoMicrosecondsPerQuarter = 1000000,
      .tracks = {PerformanceTrack{
          .events =
              {
                  TempoPerformanceEvent{.header = {.tick = 8}, .microsecondsPerQuarter = 1000000},
                  TempoPerformanceEvent{.header = {.tick = 16}, .microsecondsPerQuarter = 500000},
              }}},
  };
  const PerformanceTempoMap tempos{performance};
  performance.tracks.clear();
  const auto points = tempos.points();
  expect(
      points.size() == 3 && points[0].tick == 0 && points[0].microsecondsPerQuarter == 1000000 && points[1].tick == 8 &&
          points[1].microsecondsPerQuarter == 1000000 && points[2].tick == 16 &&
          points[2].microsecondsPerQuarter == 500000 && tempos.microsecondsPerQuarterAt(8) == 1000000,
      "tempo points must outlive source events and retain the first explicit write alongside implicit initial tempo");

  performance.tracks = {PerformanceTrack{.events = {TempoPerformanceEvent{.microsecondsPerQuarter = 750000}}}};
  const auto atStart = PerformanceTempoMap{performance}.points();
  expect(atStart.size() == 1 && atStart[0].tick == 0 && atStart[0].microsecondsPerQuarter == 750000,
         "an explicit tempo at tick zero must replace the implicit initial tempo");
  expect(PerformanceTempoMap{PerformanceSequence{}}.points().empty(),
         "the default initial tempo must not introduce a redundant output event");
}

}  // namespace

void runValueSequenceModelTests() {
  levelScaleRoundTripsMidiValues();
  byteReaderChecksBoundsAndEndian();
  sequenceSourceRangeIncludesDecodedCommandsFromTheBaseSource();
  sequenceValidationProtectsPositionalCommandStorage();
  performanceEventsForCommandMatchBothTrackAndCommand();
  performanceEmitterBindsScalarAutomationWithoutExposingStorage();
  sequenceMotionPreservesDelayAndTargetCompletion();
  fixedPointMotionRetargetsFromTheRoundedSourceValue();
  performanceBoundValueOwnsReplacementLifecycle();
  performanceEmitterResolvesDeclaredPanLawIntoEvents();
  pitchTransitionApiPreservesSamplesAndRealizedLifecycle();
  continuedVoiceResolvesPriorPitchMotion();
  previousNoteEndRetainsContinuationChainBehavior();
  tempoMapPreservesOrderingAndBoundsDurationConversion();
  physicalTimingUsesTheFullInternalDivision();
  tempoMapRetainsInitialTempoAndOwnsItsPoints();
}
