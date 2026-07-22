/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"

#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

PerformanceAutomationIntent automationIntent(PerformanceAutomationTarget target, PerformanceAutomationMotion motion,
                                             double targetValue, u32 durationTicks, u32 delayTicks,
                                             bool restartsOnNote = false) {
  return PerformanceAutomationIntent{
      .target = target,
      .motion = motion,
      .targetValue = targetValue,
      .durationTicks = durationTicks,
      .delayTicks = delayTicks,
      .restartsOnNote = restartsOnNote,
  };
}

}  // namespace

PerformanceEmitter::PerformanceEmitter(PerformanceTrack& track, CommandId sourceCommand,
                                       SourceAnnotationId sourceAnnotation, u64 tick, u64& nextSequence)
    : track_(track), sourceCommand_(sourceCommand), sourceAnnotation_(sourceAnnotation), tick_(tick),
      nextSequence_(nextSequence) {
}

PerformanceEmitter PerformanceEmitter::at(u64 tick) const {
  auto output = PerformanceEmitter{track_, sourceCommand_, sourceAnnotation_, tick, nextSequence_};
  output.automation_ = automation_;
  return output;
}

void PerformanceEmitter::note(NotePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious) {
  note(NotePerformanceEvent{
      .key = key,
      .linearVelocity = linearVelocity,
      .durationTicks = durationTicks,
      .extendsPrevious = extendsPrevious,
  });
}

void PerformanceEmitter::tempo(TempoPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::tempo(u32 microsecondsPerQuarter) {
  tempo(TempoPerformanceEvent{
      .microsecondsPerQuarter = microsecondsPerQuarter,
  });
}

void PerformanceEmitter::timeSignature(TimeSignaturePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::timeSignature(u8 numerator, u8 denominator, u8 clocksPerMetronomeClick) {
  timeSignature(TimeSignaturePerformanceEvent{
      .numerator = numerator,
      .denominator = denominator,
      .clocksPerMetronomeClick = clocksPerMetronomeClick,
  });
}

void PerformanceEmitter::instrument(InstrumentPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::instrument(InstrumentIdentity sourceInstrument) {
  instrument(InstrumentPerformanceEvent{
      .sourceInstrument = std::move(sourceInstrument),
  });
}

void PerformanceEmitter::instrument(u32 bank, u32 program, bool forceBankSelect) {
  instrument(InstrumentPerformanceEvent{
      .bank = bank,
      .program = program,
      .forceBankSelect = forceBankSelect,
  });
}

void PerformanceEmitter::level(LevelPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::level(double linearGain, ValueQuantization sourceQuantization) {
  level(LevelPerformanceEvent{
      .linearGain = linearGain,
      .sourceQuantization = sourceQuantization,
  });
}

void PerformanceEmitter::level(double linearGain, LevelPrecisionHint precisionHint) {
  level(LevelPerformanceEvent{
      .linearGain = linearGain,
      .precisionHint = precisionHint,
  });
}

void PerformanceEmitter::expression(ExpressionPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::expression(double linearGain, ValueQuantization sourceQuantization) {
  expression(ExpressionPerformanceEvent{
      .linearGain = linearGain,
      .sourceQuantization = sourceQuantization,
  });
}

void PerformanceEmitter::expression(double linearGain, LevelPrecisionHint precisionHint) {
  expression(ExpressionPerformanceEvent{
      .linearGain = linearGain,
      .precisionHint = precisionHint,
  });
}

void PerformanceEmitter::pan(PanPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::pan(double stereoPosition) {
  pan(PanPerformanceEvent{
      .stereoPosition = stereoPosition,
  });
}

void PerformanceEmitter::pan(double stereoPosition, double linearGain) {
  pan(PanPerformanceEvent{
      .stereoPosition = stereoPosition,
      .linearGain = linearGain,
      .hasLinearGain = true,
  });
}

void PerformanceEmitter::stereoBalance(StereoBalancePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::stereoBalance(double leftGain, double rightGain) {
  stereoBalance(StereoBalancePerformanceEvent{
      .leftGain = leftGain,
      .rightGain = rightGain,
  });
}

void PerformanceEmitter::masterLevel(MasterLevelPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::masterLevel(double linearGain) {
  masterLevel(MasterLevelPerformanceEvent{
      .linearGain = linearGain,
  });
}

void PerformanceEmitter::reverb(ReverbPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::reverb(double send) {
  reverb(ReverbPerformanceEvent{
      .send = send,
  });
}

void PerformanceEmitter::tuning(TuningPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::tuning(double cents) {
  tuning(TuningPerformanceEvent{
      .cents = cents,
  });
}

void PerformanceEmitter::globalTranspose(GlobalTransposePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::globalTranspose(s32 semitones) {
  globalTranspose(GlobalTransposePerformanceEvent{
      .semitones = semitones,
  });
}

void PerformanceEmitter::pitchBend(PitchBendPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::pitchBend(double semitones) {
  pitchBend(PitchBendPerformanceEvent{
      .semitones = semitones,
  });
}

void PerformanceEmitter::pitchBendRange(PitchBendRangePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::pitchBendRange(u8 semitones) {
  pitchBendRange(PitchBendRangePerformanceEvent{
      .cents = static_cast<u16>(static_cast<u16>(semitones) * 100),
  });
}

void PerformanceEmitter::vibratoDelay(VibratoDelayPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::vibratoDelay(u32 delayTicks, u8 midiValue) {
  vibratoDelay(VibratoDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .midiValue = midiValue,
  });
}

void PerformanceEmitter::tremoloDelay(TremoloDelayPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::tremoloDelay(u32 delayTicks, u8 midiValue) {
  tremoloDelay(TremoloDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .midiValue = midiValue,
  });
}

void PerformanceEmitter::portamento(PortamentoPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::portamento(double timeMilliseconds, double previousKey) {
  portamento(PortamentoPerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
      .previousKey = previousKey,
  });
}

void PerformanceEmitter::portamentoEnable(PortamentoEnablePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::portamentoEnable(bool enabled) {
  portamentoEnable(PortamentoEnablePerformanceEvent{
      .enabled = enabled,
  });
}

void PerformanceEmitter::portamentoTime(PortamentoTimePerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::portamentoTime(double timeMilliseconds) {
  portamentoTime(PortamentoTimePerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
  });
}

void PerformanceEmitter::portamentoControl(PortamentoControlPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::portamentoControl(double previousKey) {
  portamentoControl(PortamentoControlPerformanceEvent{
      .previousKey = previousKey,
  });
}

void PerformanceEmitter::legatoPedal(LegatoPedalPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::legatoPedal(bool enabled) {
  legatoPedal(LegatoPedalPerformanceEvent{
      .enabled = enabled,
  });
}

void PerformanceEmitter::modulation(ModulationPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::modulation(ModulationPerformanceTarget target, double amount) {
  modulation(ModulationPerformanceEvent{
      .target = target,
      .amount = amount,
  });
}

void PerformanceEmitter::marker(MarkerPerformanceEvent event) {
  append(std::move(event));
}

PerformanceAutomationBinding PerformanceEmitter::beginAutomation(PerformanceAutomationIntent intent) {
  const auto index = static_cast<u32>(track_.automations.size());
  track_.automations.push_back(PerformanceAutomation{
      .header = header(),
      .intent = std::move(intent),
  });
  return PerformanceAutomationBinding{track_, index};
}

PerformanceAutomationBinding PerformanceEmitter::fade(PerformanceAutomationTarget target, double targetValue,
                                                      u32 durationTicks, u32 delayTicks) {
  return beginAutomation(
      automationIntent(target, PerformanceAutomationMotion::TargetOverTicks, targetValue, durationTicks, delayTicks));
}

PerformanceAutomationBinding PerformanceEmitter::noteFade(PerformanceAutomationTarget target, double targetValue,
                                                          u32 durationTicks, u32 delayTicks) {
  return beginAutomation(automationIntent(target, PerformanceAutomationMotion::TargetOverTicks, targetValue,
                                          durationTicks, delayTicks, true));
}

PerformanceAutomationBinding PerformanceEmitter::step(PerformanceAutomationTarget target, double targetValue,
                                                      u32 durationTicks, u32 delayTicks) {
  return beginAutomation(
      automationIntent(target, PerformanceAutomationMotion::TargetByStep, targetValue, durationTicks, delayTicks));
}

PerformanceAutomationBinding PerformanceEmitter::noteEnvelope(PerformanceAutomationTarget target, double targetValue,
                                                              u32 durationTicks, u32 delayTicks) {
  return beginAutomation(
      automationIntent(target, PerformanceAutomationMotion::Envelope, targetValue, durationTicks, delayTicks, true));
}

PerformanceEmitter PerformanceEmitter::withAutomation(const PerformanceAutomationBinding& automation) const {
  auto output = *this;
  if (automation.owner_ == nullptr) {
    output.automation_.reset();
  } else if (automation.owner_ != &track_) {
    throw std::logic_error("Performance automation binding belongs to another track");
  } else {
    output.automation_ = automation.automation_;
  }
  return output;
}

void PerformanceEmitter::append(PerformanceEvent event) {
  if (automation_) {
    automationPoint(*automation_, std::move(event));
    return;
  }
  std::visit([&](auto& typedEvent) { typedEvent.header = header(); }, event);
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::automationPoint(u32 automation, PerformanceEvent event) {
  if (automation >= track_.automations.size()) {
    throw std::logic_error("Performance automation binding was not valid for this track");
  }
  const auto& origin = track_.automations[automation].header;
  std::visit(
      [&](auto& typedEvent) {
        typedEvent.header = PerformanceEventHeader{
            .sourceCommand = origin.sourceCommand,
            .sourceAnnotation = origin.sourceAnnotation,
            .track = origin.track,
            .tick = tick_,
            .sequence = nextSequence_++,
        };
      },
      event);
  track_.automations[automation].points.emplace_back(std::move(event));
}

PerformanceEventHeader PerformanceEmitter::header() {
  return PerformanceEventHeader{
      .sourceCommand = sourceCommand_,
      .sourceAnnotation = sourceAnnotation_,
      .track = track_.id,
      .tick = tick_,
      .sequence = nextSequence_++,
  };
}

}  // namespace vgmtrans::core
