/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/ModulationScaling.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool shouldScale(const ObservedValueRange* range, ModulationScalingPolicy policy) noexcept {
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
    case SynthDestination::TremoloDepth:
      return &usage.tremoloDepth;
    case SynthDestination::TremoloRate:
      return &usage.tremoloRate;
    case SynthDestination::Volume:
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

void applyMidiModulationScaling(MidiSequence& sequence, const MidiModulationUsage& usage,
                                ModulationScalingPolicy policy) {
  for (auto& track : sequence.tracks) {
    for (auto& event : track.events) {
      std::visit(
          [&](auto& typedEvent) {
            using TypedEvent = std::decay_t<decltype(typedEvent)>;
            if constexpr (std::is_same_v<TypedEvent, VibratoDepth>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, &usage.vibratoDepth, policy);
            } else if constexpr (std::is_same_v<TypedEvent, VibratoFrequency>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, &usage.vibratoRate, policy);
            } else if constexpr (std::is_same_v<TypedEvent, TremoloDepth>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, &usage.tremoloDepth, policy);
            } else if constexpr (std::is_same_v<TypedEvent, TremoloFrequency>) {
              typedEvent.value = scaledMidiModulationControllerValue(typedEvent.value, &usage.tremoloRate, policy);
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

  return static_cast<s32>(std::lround((static_cast<double>(modulator.amount) * range->max) / 127.0));
}

}  // namespace vgmtrans::core
