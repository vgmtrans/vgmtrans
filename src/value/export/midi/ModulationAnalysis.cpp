/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/ModulationAnalysis.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u32 midiControllerValue(double normalized);

void observe(ObservedValueRange& range, u32 value, double normalized, double normalizedCeiling,
             SourceRange sourceRange) {
  const double clampedNormalized = std::clamp(normalized, 0.0, 1.0);
  const double clampedCeiling = std::clamp(normalizedCeiling, clampedNormalized, 1.0);
  const u32 ceilingValue = midiControllerValue(clampedCeiling);
  if (!range.observed) {
    range.observed = true;
    range.min = value;
    range.max = std::max(value, ceilingValue);
    range.normalizedMin = clampedNormalized;
    range.normalizedMax = clampedCeiling;
    if (sourceRange.valid()) {
      range.firstRange = sourceRange;
    }
    return;
  }

  range.min = std::min(range.min, value);
  range.max = std::max(range.max, std::max(value, ceilingValue));
  range.normalizedMin = std::min(range.normalizedMin, clampedNormalized);
  range.normalizedMax = std::max(range.normalizedMax, clampedCeiling);
}

void merge(ObservedValueRange& destination, const ObservedValueRange& source) {
  if (!source.observed) {
    return;
  }
  if (!destination.observed) {
    destination = source;
    return;
  }
  destination.min = std::min(destination.min, source.min);
  destination.max = std::max(destination.max, source.max);
  destination.normalizedMin = std::min(destination.normalizedMin, source.normalizedMin);
  destination.normalizedMax = std::max(destination.normalizedMax, source.normalizedMax);
}

[[nodiscard]] u32 midiControllerValue(double normalized) {
  return static_cast<u32>(std::clamp<int>(static_cast<int>(std::lround(std::clamp(normalized, 0.0, 1.0) * 127.0)), 0,
                                          127));
}

void observePerformanceModulation(MidiTrackModulationUsage& usage, const ModulationPerformanceEvent& event) {
  const double ceiling = event.controllerRangeMaxAmount.value_or(event.amount);
  switch (event.target) {
    case ModulationPerformanceTarget::VibratoDepth:
      observe(usage.vibratoDepth, midiControllerValue(event.amount), event.amount, ceiling, SourceRange{});
      break;
    case ModulationPerformanceTarget::VibratoRate:
      observe(usage.vibratoRate, midiControllerValue(event.amount), event.amount, ceiling, SourceRange{});
      break;
    case ModulationPerformanceTarget::TremoloDepth:
      observe(usage.tremoloDepth, midiControllerValue(event.amount), event.amount, ceiling, SourceRange{});
      break;
    case ModulationPerformanceTarget::TremoloRate:
      observe(usage.tremoloRate, midiControllerValue(event.amount), event.amount, ceiling, SourceRange{});
      break;
  }
}

[[nodiscard]] double normalizedControllerAmount(u8 value, const std::optional<double>& normalizedAmount) {
  return normalizedAmount.value_or(static_cast<double>(value) / 127.0);
}

void mergeTrackUsage(MidiModulationUsage& result, const MidiTrackModulationUsage& trackUsage) {
  merge(result.vibratoDepth, trackUsage.vibratoDepth);
  merge(result.vibratoRate, trackUsage.vibratoRate);
  merge(result.tremoloDepth, trackUsage.tremoloDepth);
  merge(result.tremoloRate, trackUsage.tremoloRate);
}

}  // namespace

bool hasObservedValue(const ObservedValueRange& range) noexcept {
  return range.observed;
}

bool hasMidiModulationUsage(const MidiTrackModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed ||
         usage.tremoloDepth.observed || usage.tremoloRate.observed;
}

bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed ||
         usage.tremoloDepth.observed || usage.tremoloRate.observed;
}

MidiModulationUsage analyzePerformanceModulationUsage(const PerformanceSequence& sequence) {
  MidiModulationUsage result;
  result.tracks.reserve(sequence.tracks.size());

  for (u32 trackIndex = 0; trackIndex < sequence.tracks.size(); ++trackIndex) {
    const auto& track = sequence.tracks[trackIndex];
    MidiTrackModulationUsage trackUsage{
        .trackIndex = trackIndex,
    };

    for (const auto& event : track.events) {
      if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        observePerformanceModulation(trackUsage, *modulation);
      }
    }

    mergeTrackUsage(result, trackUsage);
    result.tracks.push_back(std::move(trackUsage));
  }

  return result;
}

MidiModulationUsage analyzeMidiModulationUsage(const MidiSequence& sequence) {
  MidiModulationUsage result;
  result.tracks.reserve(sequence.tracks.size());

  for (u32 trackIndex = 0; trackIndex < sequence.tracks.size(); ++trackIndex) {
    const auto& track = sequence.tracks[trackIndex];
    MidiTrackModulationUsage trackUsage{
        .trackIndex = trackIndex,
    };

    for (const auto& event : track.events) {
      std::visit(
          [&](const auto& typedEvent) {
            using TypedEvent = std::decay_t<decltype(typedEvent)>;
            if constexpr (std::is_same_v<TypedEvent, VibratoDepth>) {
              const double normalized = normalizedControllerAmount(typedEvent.value, typedEvent.normalizedAmount);
              observe(trackUsage.vibratoDepth, typedEvent.value, normalized, normalized, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, VibratoFrequency>) {
              const double normalized = normalizedControllerAmount(typedEvent.value, typedEvent.normalizedAmount);
              observe(trackUsage.vibratoRate, typedEvent.value, normalized, normalized, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, TremoloDepth>) {
              const double normalized = normalizedControllerAmount(typedEvent.value, typedEvent.normalizedAmount);
              observe(trackUsage.tremoloDepth, typedEvent.value, normalized, normalized, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, TremoloFrequency>) {
              const double normalized = normalizedControllerAmount(typedEvent.value, typedEvent.normalizedAmount);
              observe(trackUsage.tremoloRate, typedEvent.value, normalized, normalized, SourceRange{});
            }
          },
          event);
    }

    mergeTrackUsage(result, trackUsage);
    result.tracks.push_back(std::move(trackUsage));
  }

  return result;
}

}  // namespace vgmtrans::core
