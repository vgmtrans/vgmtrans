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

[[nodiscard]] bool shouldScale(const ObservedValueRange* range, ModulationScalingPolicy policy) noexcept {
  // Only scale when the observed maximum leaves unused controller headroom. Full-range
  // data already has the best available 7-bit resolution.
  return policy == ModulationScalingPolicy::ObservedSequenceRange && range != nullptr && range->observed &&
         range->max < 127;
}

[[nodiscard]] const ObservedValueRange* rangeForDefaultModulator(const SynthModulator& modulator,
                                                                 const MidiModulationUsage& usage) noexcept {
  if (modulator.source) {
    return nullptr;
  }

  switch (modulator.destination) {
    case SynthDestination::VibratoDepth:
      return &usage.vibratoDepth;
    case SynthDestination::VibratoRate:
      return &usage.vibratoRate;
    case SynthDestination::VibratoDelay:
      return nullptr;
    case SynthDestination::TremoloDepth:
      return &usage.tremoloDepth;
    case SynthDestination::TremoloRate:
      return &usage.tremoloRate;
    case SynthDestination::TremoloDelay:
      return nullptr;
    case SynthDestination::VolumeAttenuation:
      return &usage.tremoloDepth;
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return nullptr;
  }

  return nullptr;
}

}  // namespace

LoweredSynthModulation lowerSynthModulation(const InstrumentModulation& modulation) {
  LoweredSynthModulation lowered;

  if (modulation.vibrato) {
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
      const s32 delayAmount =
          synthAmountFromSecondsRange(vibrato.delaySeconds->minimum, vibrato.delaySeconds->maximum);
      if (delayAmount != 0) {
        lowered.modulators.push_back(SynthModulator{
            .destination = SynthDestination::VibratoDelay,
            .amount = delayAmount,
        });
      }
    }
  }

  if (modulation.tremolo) {
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
      const s32 delayAmount =
          synthAmountFromSecondsRange(tremolo.delaySeconds->minimum, tremolo.delaySeconds->maximum);
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

u8 scaledMidiModulationControllerValue(u8 value, const ObservedValueRange* range,
                                       ModulationScalingPolicy policy) noexcept {
  if (!shouldScale(range, policy)) {
    return value;
  }
  if (range->max == 0) {
    return 0;
  }

  return static_cast<u8>(
      std::clamp<s32>(static_cast<s32>(std::lround((static_cast<double>(value) * 127.0) / range->max)), 0, 127));
}

u8 scaledMidiModulationControllerValue(u8 value, std::optional<double> normalizedAmount,
                                       const ObservedValueRange* range, ModulationScalingPolicy policy) noexcept {
  if (!shouldScale(range, policy) || !normalizedAmount || range->normalizedMax <= 0.0) {
    return scaledMidiModulationControllerValue(value, range, policy);
  }

  return static_cast<u8>(std::clamp<s32>(
      static_cast<s32>(
          std::lround((std::clamp(*normalizedAmount, 0.0, range->normalizedMax) * 127.0) / range->normalizedMax)),
      0, 127));
}

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy) {
  for (auto& track : sequence.tracks) {
    for (auto& event : track.events) {
      std::visit(
          [&](auto& typedEvent) {
            using TypedEvent = std::decay_t<decltype(typedEvent)>;
            if constexpr (std::is_same_v<TypedEvent, VibratoDepth>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, typedEvent.normalizedAmount,
                                                                     &usage.vibratoDepth, policy);
            } else if constexpr (std::is_same_v<TypedEvent, VibratoFrequency>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, typedEvent.normalizedAmount,
                                                                     &usage.vibratoRate, policy);
            } else if constexpr (std::is_same_v<TypedEvent, TremoloDepth>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, typedEvent.normalizedAmount,
                                                                     &usage.tremoloDepth, policy);
            } else if constexpr (std::is_same_v<TypedEvent, TremoloFrequency>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, typedEvent.normalizedAmount,
                                                                     &usage.tremoloRate, policy);
            }
          },
          event);
    }
  }
}

s32 scaledSynthModulatorAmount(const SynthModulator& modulator, const MidiModulationUsage* usage,
                               ModulationScalingPolicy policy) noexcept {
  if (usage == nullptr) {
    return modulator.amount;
  }

  const auto* range = rangeForDefaultModulator(modulator, *usage);
  if (!shouldScale(range, policy)) {
    return modulator.amount;
  }

  // If MIDI controller values are expanded upward, the synth-side modulator amount must
  // shrink by the same ratio so the audible depth stays unchanged.
  const double normalizedMax =
      range->normalizedMax > 0.0 ? range->normalizedMax : (static_cast<double>(range->max) / 127.0);
  return static_cast<s32>(std::lround(static_cast<double>(modulator.amount) * normalizedMax));
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
