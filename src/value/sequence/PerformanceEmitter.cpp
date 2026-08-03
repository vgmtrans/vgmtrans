/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u64 addTicks(u64 tick, u32 ticks) {
  return tick > std::numeric_limits<u64>::max() - ticks ? std::numeric_limits<u64>::max() : tick + ticks;
}

[[nodiscard]] ScalarPerformanceAutomationIntent scalarAutomationIntent(PerformanceAutomationTarget target,
                                                                       PerformanceAutomationMotion motion,
                                                                       double targetValue, u32 durationTicks,
                                                                       u32 delayTicks, bool restartsOnNote = false) {
  return ScalarPerformanceAutomationIntent{
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
                                       SourceAnnotationId sourceAnnotation, u64 tick, u64& nextSequence, u32& nextNote,
                                       u32& nextAutomation, PanLaw panLaw)
    : track_(track), sourceCommand_(sourceCommand), sourceAnnotation_(sourceAnnotation), tick_(tick),
      nextSequence_(nextSequence), nextNote_(nextNote), nextAutomation_(nextAutomation), panLaw_(panLaw) {
}

PerformanceEmitter PerformanceEmitter::at(u64 tick) const {
  auto output = PerformanceEmitter{track_,        sourceCommand_, sourceAnnotation_, tick,
                                   nextSequence_, nextNote_,      nextAutomation_,   panLaw_};
  output.automation_ = automation_;
  return output;
}

PerformanceNoteId PerformanceEmitter::note(NotePerformanceEvent event) {
  if (!event.note.valid() && event.extendsPrevious) {
    for (auto previous = track_.events.rbegin(); previous != track_.events.rend(); ++previous) {
      if (const auto* note = std::get_if<NotePerformanceEvent>(&*previous)) {
        event.note = note->note;
        event.lane = note->lane;
        break;
      }
    }
  }
  if (!event.note.valid()) {
    event.note = PerformanceNoteId{nextNote_++};
  }
  if (!event.extendsPrevious) {
    interruptPitchSlidesForNewNote(event.lane);
  }
  const PerformanceNoteId note = event.note;
  append(std::move(event));
  return note;
}

PerformanceNoteId PerformanceEmitter::note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious) {
  return note(NotePerformanceEvent{
      .key = key,
      .linearVelocity = linearVelocity,
      .durationTicks = durationTicks,
      .extendsPrevious = extendsPrevious,
  });
}

PerformanceNoteId PerformanceEmitter::continueVoice(PerformanceNoteId previousNote, NotePerformanceEvent event) {
  const NotePerformanceEvent* previousEvent = nullptr;
  for (auto previous = track_.events.rbegin(); previous != track_.events.rend(); ++previous) {
    const auto* candidate = std::get_if<NotePerformanceEvent>(&*previous);
    if (candidate != nullptr && candidate->note == previousNote) {
      previousEvent = candidate;
      break;
    }
  }
  if (previousEvent == nullptr) {
    event.note = {};
    event.extendsPrevious = false;
    return note(std::move(event));
  }

  const PerformanceLaneId lane = previousEvent->lane;
  event.lane = lane;
  const double startKey =
      currentPitchTransitionKey(previousNote, lane).value_or(previousEvent->key);
  if (std::abs(startKey - event.key) < 0.000001) {
    event.note = previousNote;
    event.extendsPrevious = true;
    return note(std::move(event));
  }

  event.note = {};
  event.extendsPrevious = false;
  const double targetKey = event.key;
  const PerformanceNoteId continuedNote = note(std::move(event));
  pitchSlide(continuedNote, startKey, targetKey, PitchSlideTiming::fromTicks(0), lane)
      .continueFrom(previousNote);
  return continuedNote;
}

bool PerformanceEmitter::setPreviousNoteEnd(u64 endTick) {
  bool found = false;
  for (auto event = track_.events.rbegin(); event != track_.events.rend(); ++event) {
    auto* note = std::get_if<NotePerformanceEvent>(&*event);
    if (note == nullptr) {
      continue;
    }

    found = true;
    const u64 duration = endTick > note->header.tick ? endTick - note->header.tick : 0;
    note->durationTicks = static_cast<u32>(std::min<u64>(duration, std::numeric_limits<u32>::max()));
    if (!note->extendsPrevious) {
      break;
    }
  }
  return found;
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

void PerformanceEmitter::instrument(InstrumentIdentity sourceInstrument, InstrumentEnvelopeMode envelopeMode) {
  instrument(InstrumentPerformanceEvent{
      .sourceInstrument = std::move(sourceInstrument),
      .envelopeMode = envelopeMode,
  });
}

void PerformanceEmitter::instrument(u32 bank, u32 program, InstrumentEnvelopeMode envelopeMode) {
  instrument(bank, program, false, envelopeMode);
}

void PerformanceEmitter::instrument(u32 bank, u32 program, bool forceBankSelect,
                                    InstrumentEnvelopeMode envelopeMode) {
  instrument(InstrumentPerformanceEvent{
      .bank = bank,
      .program = program,
      .forceBankSelect = forceBankSelect,
      .envelopeMode = envelopeMode,
  });
}

void PerformanceEmitter::updateEnvelope(EnvelopeUpdate update, VoiceEnvelopeScope scope) {
  append(EnvelopePerformanceEvent{
      .update = std::move(update),
      .scope = scope,
  });
}

void PerformanceEmitter::replaceEnvelope(Envelope values, VoiceEnvelopeScope scope) {
  updateEnvelope(EnvelopeUpdate::replace(std::move(values)), scope);
}

void PerformanceEmitter::updateEnvelope(Envelope values, EnvelopeFields fields, VoiceEnvelopeScope scope) {
  updateEnvelope(EnvelopeUpdate::set(std::move(values), fields), scope);
}

void PerformanceEmitter::restoreEnvelope(EnvelopeFields fields, VoiceEnvelopeScope scope) {
  updateEnvelope(EnvelopeUpdate::restore(fields), scope);
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
  if (event.law == PanLaw::Unspecified) {
    event.law = panLaw_;
  }
  if (event.law == PanLaw::Unspecified) {
    throw std::logic_error("Positional pan requires a declared pan law");
  }
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

void PerformanceEmitter::vibratoDelayTicks(u32 delayTicks) {
  vibratoDelay(VibratoDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .tempoRelative = true,
  });
}

void PerformanceEmitter::vibratoDelayPhysical(u32 delayTicks, double milliseconds) {
  vibratoDelay(VibratoDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .milliseconds = milliseconds,
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

void PerformanceEmitter::tremoloDelayTicks(u32 delayTicks) {
  tremoloDelay(TremoloDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .tempoRelative = true,
  });
}

void PerformanceEmitter::tremoloDelayPhysical(u32 delayTicks, double milliseconds) {
  tremoloDelay(TremoloDelayPerformanceEvent{
      .delayTicks = delayTicks,
      .milliseconds = milliseconds,
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

void PerformanceEmitter::pitchTransitionSettings(PitchTransitionSettingsPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::pitchTransitionSettings(double timeMilliseconds) {
  pitchTransitionSettings(PitchTransitionSettingsPerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
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

namespace {

[[nodiscard]] ModulationPerformanceEvent physicalLfoEvent(ModulationPerformanceTarget target,
                                                          LfoPerformanceContext context) {
  return ModulationPerformanceEvent{
      .target = target,
      .amount = 0.0,
      .frequencyHz = context.frequencyHz,
      .cyclesPerTick = context.cyclesPerTick,
      .delayTicks = context.delayTicks,
      .delayMilliseconds = context.delayMilliseconds,
      .delayIsTempoRelative = context.delayIsTempoRelative,
      .waveform = context.waveform,
      .polarity = context.polarity,
      .initialPhaseCycles = context.initialPhaseCycles,
      .pitchRangeSemitones = context.pitchRangeSemitones,
      .steppedDepthAttackSteps = context.steppedDepthAttackSteps,
      .sampleImmediatelyOnNote = context.sampleImmediatelyOnNote,
      .phaseRunsAtZeroDepth = context.phaseRunsAtZeroDepth,
      .tremoloGainMode = context.tremoloGainMode,
  };
}

}  // namespace

void PerformanceEmitter::vibratoDepth(double semitones, LfoPerformanceContext context) {
  auto event = physicalLfoEvent(ModulationPerformanceTarget::VibratoDepth, std::move(context));
  event.pitchDepthSemitones = semitones;
  modulation(std::move(event));
}

void PerformanceEmitter::vibratoRate(double hertz, LfoPerformanceContext context) {
  context.frequencyHz = hertz;
  context.cyclesPerTick.reset();
  modulation(physicalLfoEvent(ModulationPerformanceTarget::VibratoRate, std::move(context)));
}

void PerformanceEmitter::vibratoRateCyclesPerTick(double cycles, LfoPerformanceContext context) {
  context.frequencyHz.reset();
  context.cyclesPerTick = cycles;
  modulation(physicalLfoEvent(ModulationPerformanceTarget::VibratoRate, std::move(context)));
}

void PerformanceEmitter::tremoloDepth(double decibels, LfoPerformanceContext context) {
  auto event = physicalLfoEvent(ModulationPerformanceTarget::TremoloDepth, std::move(context));
  event.volumeDepthDecibels = decibels;
  modulation(std::move(event));
}

void PerformanceEmitter::tremoloLinearGainDepth(double gain, LfoPerformanceContext context) {
  auto event = physicalLfoEvent(ModulationPerformanceTarget::TremoloDepth, std::move(context));
  event.volumeDepthLinearGain = gain;
  modulation(std::move(event));
}

void PerformanceEmitter::tremoloRate(double hertz, LfoPerformanceContext context) {
  context.frequencyHz = hertz;
  context.cyclesPerTick.reset();
  modulation(physicalLfoEvent(ModulationPerformanceTarget::TremoloRate, std::move(context)));
}

void PerformanceEmitter::tremoloRateCyclesPerTick(double cycles, LfoPerformanceContext context) {
  context.frequencyHz.reset();
  context.cyclesPerTick = cycles;
  modulation(physicalLfoEvent(ModulationPerformanceTarget::TremoloRate, std::move(context)));
}

void PerformanceEmitter::panLfoDepth(double depth, LfoPerformanceContext context) {
  auto event = physicalLfoEvent(ModulationPerformanceTarget::PanDepth, std::move(context));
  event.panDepth = depth;
  modulation(std::move(event));
}

void PerformanceEmitter::panLfoRate(double hertz, LfoPerformanceContext context) {
  context.frequencyHz = hertz;
  context.cyclesPerTick.reset();
  modulation(physicalLfoEvent(ModulationPerformanceTarget::PanRate, std::move(context)));
}

void PerformanceEmitter::panLfoRateCyclesPerTick(double cycles, LfoPerformanceContext context) {
  context.frequencyHz.reset();
  context.cyclesPerTick = cycles;
  modulation(physicalLfoEvent(ModulationPerformanceTarget::PanRate, std::move(context)));
}

void PerformanceEmitter::marker(MarkerPerformanceEvent event) {
  append(std::move(event));
}

void PerformanceEmitter::appendEvents(std::vector<PerformanceEvent> events) {
  for (auto& event : events) {
    append(std::move(event));
  }
}

PitchSlideBinding PerformanceEmitter::pitchSlide(PerformanceNoteId note, double startKey, double targetKey,
                                                 u32 durationTicks, PerformanceLaneId lane) {
  return pitchSlide(note, startKey, targetKey, PitchSlideTiming::fromTicks(durationTicks), lane);
}

PitchSlideBinding PerformanceEmitter::pitchSlide(PerformanceNoteId note, double startKey, double targetKey,
                                                 PitchSlideTiming timing, PerformanceLaneId lane) {
  if (!note.valid()) {
    return {};
  }

  const u32 durationTicks = timing.timelineTicks;
  const u64 start = tick_;

  // This explicit-start form trusts the caller's source-domain pitch state.
  for (auto previous = track_.automations.rbegin(); previous != track_.automations.rend(); ++previous) {
    auto* previousPitch = pitchTransitionIntent(*previous);
    if (previousPitch == nullptr || previousPitch->lane != lane || previous->realization.endTick < start) {
      continue;
    }
    if (previous->realization.endTick == start) {
      if (previousPitch->note == note || previousPitch->continuesAcrossNotes) {
        previous->realization.endReason = PerformanceAutomationEndReason::Continued;
      }
      break;
    }

    previous->realization.endTick = std::max(previous->realization.startTick, start);
    previous->realization.endReason = PerformanceAutomationEndReason::Continued;
    break;
  }

  const PerformanceAutomationId id{nextAutomation_++};
  track_.automations.push_back(PerformanceAutomation{
      .id = id,
      .header = header(),
      .intent =
          PitchTransitionIntent{
              .note = note,
              .lane = lane,
              .startKey = startKey,
              .targetKey = targetKey,
              .timing = std::move(timing),
          },
      .realization =
          PerformanceAutomationRealization{
              .startTick = start,
              .endTick = addTicks(start, durationTicks),
          },
  });
  return PitchSlideBinding{track_, static_cast<u32>(track_.automations.size() - 1)};
}

PitchSlideBinding PerformanceEmitter::retargetPitchSlide(PerformanceNoteId note, double fallbackStartKey,
                                                         double targetKey, u32 durationTicks,
                                                         PerformanceLaneId lane) {
  return retargetPitchSlide(note, fallbackStartKey, targetKey, PitchSlideTiming::fromTicks(durationTicks), lane);
}

PitchSlideBinding PerformanceEmitter::retargetPitchSlide(PerformanceNoteId note, double fallbackStartKey,
                                                         double targetKey, PitchSlideTiming timing,
                                                         PerformanceLaneId lane) {
  return pitchSlide(note, currentPitchTransitionKey(note, lane).value_or(fallbackStartKey), targetKey,
                    std::move(timing), lane);
}

std::optional<double> PerformanceEmitter::currentPitchTransitionKey(PerformanceNoteId note,
                                                                    PerformanceLaneId lane) const {
  for (auto previous = track_.automations.rbegin(); previous != track_.automations.rend(); ++previous) {
    const auto* transition = pitchTransitionIntent(*previous);
    if (transition == nullptr || transition->note != note || transition->lane != lane ||
        previous->realization.startTick > tick_) {
      continue;
    }
    const u64 realizedTick = std::min(tick_, previous->realization.endTick);
    const u64 elapsed = realizedTick - previous->realization.startTick;
    return pitchTransitionValueAt(
        *transition, static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max())));
  }
  return std::nullopt;
}

PerformanceAutomationBinding PerformanceEmitter::beginAutomation(ScalarPerformanceAutomationIntent intent) {
  const auto index = static_cast<u32>(track_.automations.size());
  const PerformanceAutomationId id{nextAutomation_++};
  const u64 startTick = addTicks(tick_, intent.delayTicks);
  const u64 endTick = intent.motion == PerformanceAutomationMotion::TargetByStep || intent.durationTicks == 0
                          ? startTick
                          : addTicks(startTick, intent.durationTicks);
  track_.automations.push_back(PerformanceAutomation{
      .id = id,
      .header = header(),
      .intent = std::move(intent),
      .realization =
          PerformanceAutomationRealization{
              .startTick = startTick,
              .endTick = endTick,
          },
  });
  return PerformanceAutomationBinding{track_, index};
}

PerformanceAutomationBinding PerformanceEmitter::fade(PerformanceAutomationTarget target, double targetValue,
                                                      u32 durationTicks, u32 delayTicks) {
  return beginAutomation(scalarAutomationIntent(target, PerformanceAutomationMotion::TargetOverTicks, targetValue,
                                                durationTicks, delayTicks));
}

PerformanceAutomationBinding PerformanceEmitter::noteFade(PerformanceAutomationTarget target, double targetValue,
                                                          u32 durationTicks, u32 delayTicks) {
  return beginAutomation(scalarAutomationIntent(target, PerformanceAutomationMotion::TargetOverTicks, targetValue,
                                                durationTicks, delayTicks, true));
}

PerformanceAutomationBinding PerformanceEmitter::step(PerformanceAutomationTarget target, double targetValue,
                                                      u32 durationTicks, u32 delayTicks) {
  return beginAutomation(scalarAutomationIntent(target, PerformanceAutomationMotion::TargetByStep, targetValue,
                                                durationTicks, delayTicks));
}

PerformanceAutomationBinding PerformanceEmitter::noteEnvelope(PerformanceAutomationTarget target, double targetValue,
                                                              u32 durationTicks, u32 delayTicks) {
  return beginAutomation(scalarAutomationIntent(target, PerformanceAutomationMotion::Envelope, targetValue,
                                                durationTicks, delayTicks, true));
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
    if (*automation_ >= track_.automations.size()) {
      throw std::logic_error("Performance automation binding was not valid for this track");
    }
    auto& automation = track_.automations[*automation_];
    automation.realization.endTick = std::max(automation.realization.endTick, tick_);
  }
  const bool physicalModulation =
      std::visit(
          [](const auto& typedEvent) {
            using T = std::decay_t<decltype(typedEvent)>;
            if constexpr (std::is_same_v<T, ModulationPerformanceEvent>) {
              return typedEvent.pitchDepthSemitones.has_value() || typedEvent.volumeDepthDecibels.has_value() ||
                     typedEvent.volumeDepthLinearGain.has_value() || typedEvent.panDepth.has_value() ||
                     typedEvent.frequencyHz.has_value() || typedEvent.cyclesPerTick.has_value();
            } else if constexpr (std::is_same_v<T, VibratoDelayPerformanceEvent> ||
                                 std::is_same_v<T, TremoloDelayPerformanceEvent>) {
              return typedEvent.milliseconds.has_value() || typedEvent.tempoRelative;
            }
            return false;
          },
          event);
  track_.hasPhysicalModulation |= physicalModulation;
  std::visit([&](auto& typedEvent) { typedEvent.header = header(); }, event);
  track_.events.emplace_back(std::move(event));
}

void PerformanceEmitter::automationSample(u32 automation, double value) {
  if (automation >= track_.automations.size()) {
    throw std::logic_error("Performance automation binding was not valid for this track");
  }
  auto* pitch = pitchTransitionIntent(track_.automations[automation]);
  if (pitch == nullptr) {
    throw std::logic_error("Only pitch-transition automation accepts scalar pitch samples");
  }
  if (!std::holds_alternative<SampledAutomationCurve>(pitch->curve)) {
    SampledAutomationCurve sampled;
    sampled.samples.push_back(AutomationSample{
        .tickOffset = 0,
        .value = pitch->startKey,
    });
    if (pitch->timing.timelineTicks != 0) {
      sampled.samples.push_back(AutomationSample{
          .tickOffset = pitch->timing.timelineTicks,
          .value = pitch->targetKey,
      });
    }
    pitch->curve = std::move(sampled);
  }
  auto& samples = std::get<SampledAutomationCurve>(pitch->curve).samples;
  const u64 startTick = track_.automations[automation].realization.startTick;
  const u64 elapsed = tick_ > startTick ? tick_ - startTick : 0;
  const u32 offset = static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max()));
  const auto found = std::ranges::lower_bound(samples, offset, {}, &AutomationSample::tickOffset);
  if (found != samples.end() && found->tickOffset == offset) {
    found->value = value;
  } else {
    samples.insert(found, AutomationSample{
                              .tickOffset = offset,
                              .value = value,
                          });
  }
}

void PerformanceAutomationBinding::stop(const PerformanceEmitter& out) const {
  if (owner_ == nullptr) {
    return;
  }
  if (owner_ != &out.track_) {
    throw std::logic_error("Performance automation binding belongs to another track");
  }
  stopAt(out.tick_);
}

void PerformanceAutomationBinding::stopAt(u64 tick) const {
  if (owner_ == nullptr) {
    return;
  }
  if (automation_ >= owner_->automations.size()) {
    throw std::logic_error("Performance automation binding was not valid for its track");
  }
  auto& realization = owner_->automations[automation_].realization;
  if (tick >= realization.endTick) {
    return;
  }
  realization.endTick = std::max(realization.startTick, tick);
  realization.endReason = PerformanceAutomationEndReason::Interrupted;
}

void PerformanceAutomationBinding::replaceWith(PerformanceAutomationBinding binding) {
  if (binding.valid()) {
    if (binding.automation_ >= binding.owner_->automations.size()) {
      throw std::logic_error("Performance automation binding was not valid for its track");
    }
    stopAt(binding.owner_->automations[binding.automation_].header.tick);
  }
  *this = std::move(binding);
}

void PerformanceAutomationBinding::interrupt(const PerformanceEmitter& out) {
  stop(out);
  clear();
}

void PerformanceAutomationBinding::interruptAt(u64 tick) {
  stopAt(tick);
  clear();
}

void PerformanceAutomationBinding::sample(const PerformanceEmitter& out, double value) const {
  if (owner_ == nullptr) {
    return;
  }
  if (owner_ != &out.track_) {
    throw std::logic_error("Performance automation binding belongs to another track");
  }
  auto output = out;
  output.automationSample(automation_, value);
}

PitchTransitionIntent* PitchSlideBinding::intent() const {
  if (owner_ == nullptr) {
    return nullptr;
  }
  if (automation_ >= owner_->automations.size()) {
    throw std::logic_error("Pitch-slide binding did not reference a valid automation");
  }
  auto* transition = pitchTransitionIntent(owner_->automations[automation_]);
  if (transition == nullptr) {
    throw std::logic_error("Pitch-slide binding did not reference a pitch transition");
  }
  return transition;
}

PitchSlideBinding& PitchSlideBinding::continueFrom(PerformanceNoteId previousNote) {
  if (auto* transition = intent()) {
    transition->previousNote = previousNote.valid() ? std::optional{previousNote} : std::nullopt;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::continueAcrossNotes(bool enabled) {
  if (auto* transition = intent()) {
    transition->continuesAcrossNotes = enabled;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::preferPortamento() {
  if (auto* transition = intent()) {
    transition->preferredRendering = PitchTransitionRenderingHint::Portamento;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::preferPitchBend() {
  if (auto* transition = intent()) {
    transition->preferredRendering = PitchTransitionRenderingHint::PitchBend;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::useCurrentPortamentoTiming() {
  if (auto* transition = intent()) {
    transition->nativePortamento.useCurrentTiming = true;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::restorePortamentoTiming(double timeMilliseconds) {
  if (auto* transition = intent()) {
    transition->nativePortamento.restoreTimeMilliseconds = timeMilliseconds;
  }
  return *this;
}

PitchSlideBinding& PitchSlideBinding::portamentoOverlap(u32 ticks) {
  if (auto* transition = intent()) {
    transition->nativePortamento.overlapTicks = ticks;
  }
  return *this;
}

void PerformanceEmitter::interruptPitchSlidesForNewNote(PerformanceLaneId lane) {
  for (auto& automation : track_.automations) {
    auto* pitch = pitchTransitionIntent(automation);
    if (pitch == nullptr || pitch->lane != lane || pitch->continuesAcrossNotes ||
        automation.realization.endTick <= tick_) {
      continue;
    }
    automation.realization.endTick = std::max(automation.realization.startTick, tick_);
    automation.realization.endReason = PerformanceAutomationEndReason::Interrupted;
  }
}

PerformanceEventHeader PerformanceEmitter::header() {
  if (automation_) {
    if (*automation_ >= track_.automations.size()) {
      throw std::logic_error("Performance automation binding was not valid for this track");
    }
    const auto& automation = track_.automations[*automation_];
    return PerformanceEventHeader{
        .sourceCommand = automation.header.sourceCommand,
        .sourceAnnotation = automation.header.sourceAnnotation,
        .track = automation.header.track,
        .tick = tick_,
        .sequence = nextSequence_++,
        .automation = automation.id,
    };
  }
  return PerformanceEventHeader{
      .sourceCommand = sourceCommand_,
      .sourceAnnotation = sourceAnnotation_,
      .track = track_.id,
      .tick = tick_,
      .sequence = nextSequence_++,
  };
}

}  // namespace vgmtrans::core
