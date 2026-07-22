/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"

#include <iterator>
#include <utility>

namespace vgmtrans::core {

PerformanceEmitter::PerformanceEmitter(PerformanceTrack& track, CommandId sourceCommand,
                                       SourceAnnotationId sourceAnnotation, u64 tick)
    : track_(track), sourceCommand_(sourceCommand), sourceAnnotation_(sourceAnnotation), tick_(tick) {
}

PerformanceEmitter PerformanceEmitter::at(u64 tick) const {
  return PerformanceEmitter{track_, sourceCommand_, sourceAnnotation_, tick};
}

void PerformanceEmitter::note(NotePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::tempo(u32 microsecondsPerQuarter) {
  tempo(TempoPerformanceEvent{
      .microsecondsPerQuarter = microsecondsPerQuarter,
  });
}

void PerformanceEmitter::timeSignature(TimeSignaturePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::timeSignature(u8 numerator, u8 denominator, u8 clocksPerMetronomeClick) {
  timeSignature(TimeSignaturePerformanceEvent{
      .numerator = numerator,
      .denominator = denominator,
      .clocksPerMetronomeClick = clocksPerMetronomeClick,
  });
}

void PerformanceEmitter::instrument(InstrumentPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::stereoBalance(double leftGain, double rightGain) {
  stereoBalance(StereoBalancePerformanceEvent{
      .leftGain = leftGain,
      .rightGain = rightGain,
  });
}

void PerformanceEmitter::masterLevel(MasterLevelPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::masterLevel(double linearGain) {
  masterLevel(MasterLevelPerformanceEvent{
      .linearGain = linearGain,
  });
}

void PerformanceEmitter::reverb(ReverbPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::reverb(double send) {
  reverb(ReverbPerformanceEvent{
      .send = send,
  });
}

void PerformanceEmitter::tuning(TuningPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::tuning(double cents) {
  tuning(TuningPerformanceEvent{
      .cents = cents,
  });
}

void PerformanceEmitter::globalTranspose(GlobalTransposePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::globalTranspose(s32 semitones) {
  globalTranspose(GlobalTransposePerformanceEvent{
      .semitones = semitones,
  });
}

void PerformanceEmitter::pitchBend(PitchBendPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::pitchBend(double semitones) {
  pitchBend(PitchBendPerformanceEvent{
      .semitones = semitones,
  });
}

void PerformanceEmitter::pitchBendRange(PitchBendRangePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::pitchBendRange(u8 semitones) {
  pitchBendRange(PitchBendRangePerformanceEvent{
      .cents = static_cast<u16>(static_cast<u16>(semitones) * 100),
  });
}

void PerformanceEmitter::vibratoDelay(VibratoDelayPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::vibratoDelay(u32 delayTicks, u8 midiValue) {
  vibratoDelay(VibratoDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .midiValue = midiValue,
  });
}

void PerformanceEmitter::tremoloDelay(TremoloDelayPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::tremoloDelay(u32 delayTicks, u8 midiValue) {
  tremoloDelay(TremoloDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .midiValue = midiValue,
  });
}

void PerformanceEmitter::portamento(PortamentoPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::portamento(double timeMilliseconds, double previousKey) {
  portamento(PortamentoPerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
      .previousKey = previousKey,
  });
}

void PerformanceEmitter::portamentoEnable(PortamentoEnablePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::portamentoEnable(bool enabled) {
  portamentoEnable(PortamentoEnablePerformanceEvent{
      .enabled = enabled,
  });
}

void PerformanceEmitter::portamentoTime(PortamentoTimePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::portamentoTime(double timeMilliseconds) {
  portamentoTime(PortamentoTimePerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
  });
}

void PerformanceEmitter::portamentoControl(PortamentoControlPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::portamentoControl(double previousKey) {
  portamentoControl(PortamentoControlPerformanceEvent{
      .previousKey = previousKey,
  });
}

void PerformanceEmitter::legatoPedal(LegatoPedalPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::legatoPedal(bool enabled) {
  legatoPedal(LegatoPedalPerformanceEvent{
      .enabled = enabled,
  });
}

void PerformanceEmitter::modulation(ModulationPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::modulation(ModulationPerformanceTarget target, double amount) {
  modulation(ModulationPerformanceEvent{
      .target = target,
      .amount = amount,
  });
}

void PerformanceEmitter::marker(MarkerPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::appendEvents(std::vector<PerformanceEvent> events) {
  track_.events.insert(track_.events.end(), std::make_move_iterator(events.begin()),
                       std::make_move_iterator(events.end()));
}

PerformanceEventHeader PerformanceEmitter::header() const {
  return PerformanceEventHeader{
      .sourceCommand = sourceCommand_,
      .sourceAnnotation = sourceAnnotation_,
      .track = track_.id,
      .tick = tick_,
  };
}

}  // namespace vgmtrans::core
