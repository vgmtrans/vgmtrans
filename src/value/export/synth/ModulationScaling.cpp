/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/ModulationScaling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace vgmtrans::core {

namespace {

s32 synthAmountFromSecondsRange(double minSeconds, double maxSeconds) {
  const s32 minAmount = synthAmountFromSeconds(synthSecondsRangeMinimum(minSeconds));
  const s32 maxAmount = synthAmountFromSeconds(maxSeconds);
  const double fullScaleRange = (maxAmount - minAmount) * 128.0 / 127.0;
  return static_cast<s32>(std::lround(fullScaleRange));
}

[[nodiscard]] u8 midiControllerValue(double normalized) noexcept {
  return static_cast<u8>(std::lround(std::clamp(normalized, 0.0, 1.0) * 127.0));
}

[[nodiscard]] bool shouldScale(std::optional<double> maximum, ModulationScalingPolicy policy) noexcept {
  // Only scale when the observed maximum leaves unused controller headroom. Full-range
  // data already has the best available 7-bit resolution.
  return policy == ModulationScalingPolicy::ObservedSequenceRange && maximum && midiControllerValue(*maximum) < 127;
}

[[nodiscard]] u8 scaledMidiModulationControllerValue(u8 value, std::optional<double> normalizedAmount,
                                                     std::optional<double> maximum,
                                                     ModulationScalingPolicy policy) noexcept {
  if (!shouldScale(maximum, policy)) {
    return value;
  }
  double amount = value;
  double range = midiControllerValue(*maximum);
  if (normalizedAmount && *maximum > 0.0) {
    amount = std::clamp(*normalizedAmount, 0.0, *maximum);
    range = *maximum;
  }
  return range > 0.0 ? static_cast<u8>(std::clamp<long>(std::lround(amount * 127.0 / range), 0, 127)) : 0;
}

[[nodiscard]] std::optional<double> maximumForDefaultModulator(const SynthModulator& modulator,
                                                               const MidiModulationUsage& usage) noexcept {
  if (modulator.source != SynthSource::DefaultController) {
    return std::nullopt;
  }

  switch (modulator.destination) {
    case SynthDestination::VibratoDepth:
      return usage.vibratoDepth;
    case SynthDestination::VibratoRate:
      return usage.vibratoRate;
    case SynthDestination::VibratoDelay:
      return std::nullopt;
    case SynthDestination::TremoloDepth:
      return usage.tremoloDepth;
    case SynthDestination::TremoloRate:
      return usage.tremoloRate;
    case SynthDestination::TremoloDelay:
      return std::nullopt;
    case SynthDestination::VolumeAttenuation:
      return usage.tremoloDepth;
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] bool canUseNativeSynthLfo(std::optional<LfoWaveform> waveform) noexcept {
  // SF2 and DLS cannot select an LFO waveform. Their built-in periodic LFO is
  // still a useful approximation for sine, square, triangle, and sawtooth.
  // Noise has no comparable native representation.
  return waveform != LfoWaveform::Noise;
}

}  // namespace

s32 synthAmountFromHertz(double hertz) {
  // SF2 and DLS express LFO frequency in absolute cents relative to C-1.
  return static_cast<s32>(std::lround(1200.0 * std::log2(hertz / 8.176)));
}

s32 synthAmountFromHertzRange(double minHertz, double maxHertz) {
  const double minCents = static_cast<double>(synthAmountFromHertz(minHertz));
  const double maxCents = static_cast<double>(synthAmountFromHertz(maxHertz));
  return static_cast<s32>(std::lround((maxCents - minCents) * 128.0 / 127.0));
}

s32 synthAmountFromSeconds(double seconds) {
  if (seconds <= 0.0 || !std::isfinite(seconds)) {
    return std::numeric_limits<s16>::min();
  }

  const double timecents = std::round(1200.0 * std::log2(seconds));
  return static_cast<s32>(std::clamp(timecents, static_cast<double>(std::numeric_limits<s16>::min()),
                                     static_cast<double>(std::numeric_limits<s16>::max())));
}

s32 synthAmountFromCentibels(double centibels) {
  return static_cast<s32>(std::lround(centibels));
}

s32 synthAmountFromDecibels(double decibels) {
  return static_cast<s32>(std::lround(decibels * 10.0));
}

double synthSecondsRangeMinimum(double seconds) {
  // The smallest normal SF2 delay is -12000 timecents.
  return std::max(seconds, 1.0 / 1024.0);
}

LoweredSynthModulation lowerSynthModulation(const InstrumentModulation& modulation,
                                            ModulationConversionPolicy conversion) {
  LoweredSynthModulation lowered;
  const bool nativeLfo = conversion != ModulationConversionPolicy::SequenceEventSimulation;

  const auto addTimingGenerators = [&](const auto& lfo, SynthDestination rate, SynthDestination delay) {
    lowered.generators.push_back(SynthGenerator{
        .destination = rate,
        .amount = synthAmountFromHertz(lfo.rateHertz.minimum),
    });
    if (lfo.delaySeconds) {
      lowered.generators.push_back(SynthGenerator{
          .destination = delay,
          .amount = synthAmountFromSeconds(synthSecondsRangeMinimum(lfo.delaySeconds->minimum)),
      });
    }
  };
  const auto addTimingModulators = [&](const auto& lfo, SynthDestination rate, SynthDestination delay) {
    const s32 rateAmount = synthAmountFromHertzRange(lfo.rateHertz.minimum, lfo.rateHertz.maximum);
    if (rateAmount != 0) {
      lowered.modulators.push_back(SynthModulator{.destination = rate, .amount = rateAmount});
    }
    if (lfo.delaySeconds) {
      const s32 delayAmount = synthAmountFromSecondsRange(lfo.delaySeconds->minimum, lfo.delaySeconds->maximum);
      if (delayAmount != 0) {
        lowered.modulators.push_back(SynthModulator{.destination = delay, .amount = delayAmount});
      }
    }
  };
  const auto addDepth = [&](SynthDestination destination, s32 amount, ModulationDepthMode mode) {
    if (mode == ModulationDepthMode::Fixed) {
      lowered.generators.push_back(SynthGenerator{.destination = destination, .amount = amount});
    } else {
      lowered.modulators.push_back(SynthModulator{.destination = destination, .amount = amount});
    }
  };

  if (nativeLfo && modulation.vibrato && canUseNativeSynthLfo(modulation.vibrato->waveform)) {
    const auto& vibrato = *modulation.vibrato;
    addTimingGenerators(vibrato, SynthDestination::VibratoRate, SynthDestination::VibratoDelay);
    if (vibrato.depthMode != ModulationDepthMode::Fixed) {
      lowered.modulators.push_back(SynthModulator{
          .source = SynthSource::ChannelPressure,
          .destination = SynthDestination::VibratoDepth,
          .amount = 0,
      });
    }
    addDepth(SynthDestination::VibratoDepth, static_cast<s32>(std::lround(vibrato.maxDepthCents)), vibrato.depthMode);
    addTimingModulators(vibrato, SynthDestination::VibratoRate, SynthDestination::VibratoDelay);
  }

  if (modulation.tremolo && canUseNativeSynthLfo(modulation.tremolo->waveform)) {
    const auto& tremolo = *modulation.tremolo;
    const s32 depth = synthAmountFromDecibels(tremolo.maxDepthDb);
    if (nativeLfo) {
      addTimingGenerators(tremolo, SynthDestination::TremoloRate, SynthDestination::TremoloDelay);
      addTimingModulators(tremolo, SynthDestination::TremoloRate, SynthDestination::TremoloDelay);
      addDepth(SynthDestination::TremoloDepth, depth, tremolo.depthMode);
    }
    // Simulated modulation still needs the static attenuation that places a
    // fixed no-boost tremolo below nominal gain.
    if (tremolo.gainMode == TremoloGainMode::NoBoost &&
        (nativeLfo || tremolo.depthMode == ModulationDepthMode::Fixed)) {
      addDepth(SynthDestination::VolumeAttenuation, depth, tremolo.depthMode);
    }
  }
  return lowered;
}

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy) {
  if (!hasMidiModulationUsage(usage)) {
    return;
  }
  for (auto& track : sequence.tracks) {
    for (auto& event : track.events) {
      auto* message = std::get_if<MidiChannelMessage>(&event.payload);
      if (message == nullptr || message->kind != MidiChannelMessageKind::ControlChange) {
        continue;
      }
      std::optional<double> observedMaximum;
      switch (static_cast<MidiController>(message->parameter)) {
        case MidiController::Modulation:
          observedMaximum = usage.vibratoDepth;
          break;
        case MidiController::VibratoRate:
          observedMaximum = usage.vibratoRate;
          break;
        case MidiController::TremoloDepth:
          observedMaximum = usage.tremoloDepth;
          break;
        case MidiController::TremoloRate:
          observedMaximum = usage.tremoloRate;
          break;
        default:
          continue;
      }
      message->value = scaledMidiModulationControllerValue(static_cast<u8>(message->value), message->normalizedAmount,
                                                           observedMaximum, policy);
    }
  }
}

s32 scaledSynthModulatorAmount(const SynthModulator& modulator, const MidiModulationUsage* usage,
                               ModulationScalingPolicy policy) noexcept {
  if (usage == nullptr) {
    return modulator.amount;
  }

  const auto observedMaximum = maximumForDefaultModulator(modulator, *usage);
  if (!shouldScale(observedMaximum, policy)) {
    return modulator.amount;
  }

  // If MIDI controller values are expanded upward, the synth-side modulator amount must
  // shrink by the same ratio so the audible depth stays unchanged.
  return static_cast<s32>(std::lround(static_cast<double>(modulator.amount) * *observedMaximum));
}

}  // namespace vgmtrans::core
