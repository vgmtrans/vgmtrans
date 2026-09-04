/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/ModulationScaling.h"

#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <type_traits>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool shouldScale(const MidiModulationMaximum* maximum, ModulationScalingPolicy policy) noexcept {
  // Only scale when the observed maximum leaves unused controller headroom. Full-range
  // data already has the best available 7-bit resolution.
  return policy == ModulationScalingPolicy::ObservedSequenceRange && maximum != nullptr &&
         maximum->controllerValue < 127;
}

[[nodiscard]] const MidiModulationMaximum* maximum(const std::optional<MidiModulationMaximum>& value) noexcept {
  return value ? &*value : nullptr;
}

[[nodiscard]] const MidiModulationMaximum* maximumForDefaultModulator(const SynthModulator& modulator,
                                                                      const MidiModulationUsage& usage) noexcept {
  if (modulator.source) {
    return nullptr;
  }

  switch (modulator.destination) {
    case SynthDestination::VibratoDepth:
      return maximum(usage.vibratoDepth);
    case SynthDestination::VibratoRate:
      return maximum(usage.vibratoRate);
    case SynthDestination::VibratoDelay:
      return nullptr;
    case SynthDestination::TremoloDepth:
      return maximum(usage.tremoloDepth);
    case SynthDestination::TremoloRate:
      return maximum(usage.tremoloRate);
    case SynthDestination::TremoloDelay:
      return nullptr;
    case SynthDestination::VolumeAttenuation:
      return maximum(usage.tremoloDepth);
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return nullptr;
  }

  return nullptr;
}

[[nodiscard]] bool canUseNativeSynthLfo(std::optional<LfoWaveform> waveform) noexcept {
  // SF2 and DLS cannot select an LFO waveform. Their built-in periodic LFO is
  // still a useful approximation for sine, square, triangle, and sawtooth.
  // Noise has no comparable native representation.
  return waveform != LfoWaveform::Noise;
}

}  // namespace

LoweredSynthModulation lowerSynthModulation(const InstrumentModulation& modulation) {
  LoweredSynthModulation lowered;

  if (modulation.vibrato && canUseNativeSynthLfo(modulation.vibrato->waveform)) {
    const auto& vibrato = *modulation.vibrato;
    lowered.generators.push_back(SynthGenerator{
        .destination = SynthDestination::VibratoRate,
        .amount = synthAmountFromHertz(vibrato.rateHertz.minimum),
    });
    if (vibrato.delaySeconds) {
      lowered.generators.push_back(SynthGenerator{
          .destination = SynthDestination::VibratoDelay,
          .amount = synthAmountFromSeconds(synthSecondsRangeMinimum(vibrato.delaySeconds->minimum)),
      });
    }

    if (vibrato.depthMode == ModulationDepthMode::Fixed) {
      lowered.generators.push_back(SynthGenerator{
          .destination = SynthDestination::VibratoDepth,
          .amount = static_cast<s32>(std::lround(vibrato.maxDepthCents)),
      });
    } else {
      lowered.modulators.push_back(SynthModulator{
          .source = SynthSource::ChannelPressure,
          .destination = SynthDestination::VibratoDepth,
          .amount = 0,
      });
      lowered.modulators.push_back(SynthModulator{
          .destination = SynthDestination::VibratoDepth,
          .amount = static_cast<s32>(std::lround(vibrato.maxDepthCents)),
      });
    }
    const s32 rateAmount = synthAmountFromHertzRange(vibrato.rateHertz.minimum, vibrato.rateHertz.maximum);
    if (rateAmount != 0) {
      lowered.modulators.push_back(SynthModulator{
          .destination = SynthDestination::VibratoRate,
          .amount = rateAmount,
      });
    }
    if (vibrato.delaySeconds) {
      const s32 delayAmount = synthAmountFromSecondsRange(vibrato.delaySeconds->minimum, vibrato.delaySeconds->maximum);
      if (delayAmount != 0) {
        lowered.modulators.push_back(SynthModulator{
            .destination = SynthDestination::VibratoDelay,
            .amount = delayAmount,
        });
      }
    }
  }

  if (modulation.tremolo && canUseNativeSynthLfo(modulation.tremolo->waveform)) {
    const auto& tremolo = *modulation.tremolo;
    lowered.generators.push_back(SynthGenerator{
        .destination = SynthDestination::TremoloRate,
        .amount = synthAmountFromHertz(tremolo.rateHertz.minimum),
    });
    if (tremolo.delaySeconds) {
      lowered.generators.push_back(SynthGenerator{
          .destination = SynthDestination::TremoloDelay,
          .amount = synthAmountFromSeconds(synthSecondsRangeMinimum(tremolo.delaySeconds->minimum)),
      });
    }

    const s32 rateAmount = synthAmountFromHertzRange(tremolo.rateHertz.minimum, tremolo.rateHertz.maximum);
    if (rateAmount != 0) {
      lowered.modulators.push_back(SynthModulator{
          .destination = SynthDestination::TremoloRate,
          .amount = rateAmount,
      });
    }
    if (tremolo.delaySeconds) {
      const s32 delayAmount = synthAmountFromSecondsRange(tremolo.delaySeconds->minimum, tremolo.delaySeconds->maximum);
      if (delayAmount != 0) {
        lowered.modulators.push_back(SynthModulator{
            .destination = SynthDestination::TremoloDelay,
            .amount = delayAmount,
        });
      }
    }
    const s32 tremoloDepth = synthAmountFromDecibels(tremolo.maxDepthDb);
    if (tremolo.depthMode == ModulationDepthMode::Fixed) {
      lowered.generators.push_back(SynthGenerator{
          .destination = SynthDestination::TremoloDepth,
          .amount = tremoloDepth,
      });
    } else {
      lowered.modulators.push_back(SynthModulator{
          .destination = SynthDestination::TremoloDepth,
          .amount = tremoloDepth,
      });
    }
    if (tremolo.gainMode == TremoloGainMode::NoBoost) {
      if (tremolo.depthMode == ModulationDepthMode::Fixed) {
        lowered.generators.push_back(SynthGenerator{
            .destination = SynthDestination::VolumeAttenuation,
            .amount = tremoloDepth,
        });
      } else {
        lowered.modulators.push_back(SynthModulator{
            .destination = SynthDestination::VolumeAttenuation,
            .amount = tremoloDepth,
        });
      }
    }
  }

  return lowered;
}

u8 scaledMidiModulationControllerValue(u8 value, const MidiModulationMaximum* maximum,
                                       ModulationScalingPolicy policy) noexcept {
  if (!shouldScale(maximum, policy)) {
    return value;
  }
  if (maximum->controllerValue == 0) {
    return 0;
  }

  return static_cast<u8>(std::clamp<s32>(
      static_cast<s32>(std::lround((static_cast<double>(value) * 127.0) / maximum->controllerValue)), 0, 127));
}

u8 scaledMidiModulationControllerValue(u8 value, std::optional<double> normalizedAmount,
                                       const MidiModulationMaximum* maximum, ModulationScalingPolicy policy) noexcept {
  if (!shouldScale(maximum, policy) || !normalizedAmount || maximum->normalized <= 0.0) {
    return scaledMidiModulationControllerValue(value, maximum, policy);
  }

  return static_cast<u8>(std::clamp<s32>(
      static_cast<s32>(
          std::lround((std::clamp(*normalizedAmount, 0.0, maximum->normalized) * 127.0) / maximum->normalized)),
      0, 127));
}

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy) {
  for (auto& track : sequence.tracks) {
    for (auto& event : track.events) {
      auto* message = std::get_if<MidiChannelMessage>(&event.payload);
      if (message == nullptr || message->kind != MidiChannelMessageKind::ControlChange) {
        continue;
      }
      const MidiModulationMaximum* observedMaximum = nullptr;
      switch (static_cast<MidiController>(message->parameter)) {
        case MidiController::Modulation:
          observedMaximum = maximum(usage.vibratoDepth);
          break;
        case MidiController::VibratoRate:
          observedMaximum = maximum(usage.vibratoRate);
          break;
        case MidiController::TremoloDepth:
          observedMaximum = maximum(usage.tremoloDepth);
          break;
        case MidiController::TremoloRate:
          observedMaximum = maximum(usage.tremoloRate);
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

  const auto* observedMaximum = maximumForDefaultModulator(modulator, *usage);
  if (!shouldScale(observedMaximum, policy)) {
    return modulator.amount;
  }

  // If MIDI controller values are expanded upward, the synth-side modulator amount must
  // shrink by the same ratio so the audible depth stays unchanged.
  const double normalizedMaximum = observedMaximum->normalized > 0.0
                                       ? observedMaximum->normalized
                                       : (static_cast<double>(observedMaximum->controllerValue) / 127.0);
  return static_cast<s32>(std::lround(static_cast<double>(modulator.amount) * normalizedMaximum));
}

bool shouldExportSynthGenerator(const SynthGenerator& generator, ModulationConversionPolicy conversion) noexcept {
  if (conversion == ModulationConversionPolicy::SynthModulators) {
    return true;
  }

  switch (generator.destination) {
    case SynthDestination::VibratoDepth:
    case SynthDestination::VibratoRate:
    case SynthDestination::VibratoDelay:
    case SynthDestination::TremoloDepth:
    case SynthDestination::TremoloRate:
    case SynthDestination::TremoloDelay:
      return false;
    case SynthDestination::VolumeAttenuation:
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return true;
  }
  return true;
}

bool shouldExportSynthModulator(const SynthModulator& modulator, ModulationConversionPolicy conversion) noexcept {
  if (conversion == ModulationConversionPolicy::SynthModulators) {
    return true;
  }

  switch (modulator.destination) {
    case SynthDestination::VibratoDepth:
    case SynthDestination::VibratoRate:
    case SynthDestination::VibratoDelay:
    case SynthDestination::TremoloDepth:
    case SynthDestination::TremoloRate:
    case SynthDestination::TremoloDelay:
      return false;
    case SynthDestination::VolumeAttenuation:
      return modulator.source.has_value();
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return true;
  }
  return true;
}

}  // namespace vgmtrans::core
