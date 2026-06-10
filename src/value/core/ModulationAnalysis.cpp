/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/ModulationAnalysis.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

void observe(ObservedValueRange& range, u32 value, SourceRange sourceRange) {
  if (!range.observed) {
    range.observed = true;
    range.min = value;
    range.max = value;
    if (sourceRange.valid()) {
      range.firstRange = sourceRange;
    }
    return;
  }

  range.min = std::min(range.min, value);
  range.max = std::max(range.max, value);
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
}

}  // namespace

bool hasObservedValue(const ObservedValueRange& range) noexcept {
  return range.observed;
}

bool hasModulationUsage(const TrackModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.tremoloDepth.observed || usage.modulationRate.observed;
}

bool hasModulationUsage(const ModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.tremoloDepth.observed || usage.modulationRate.observed;
}

bool hasMidiModulationUsage(const MidiTrackModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed ||
         usage.tremoloDepth.observed || usage.tremoloRate.observed;
}

bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept {
  return usage.vibratoDepth.observed || usage.vibratoRate.observed ||
         usage.tremoloDepth.observed || usage.tremoloRate.observed;
}

ModulationUsage analyzeModulationUsage(const CommandSequence& sequence) {
  ModulationUsage result;
  result.tracks.reserve(sequence.tracks.size());

  for (const auto& track : sequence.tracks) {
    TrackModulationUsage trackUsage{
        .sourceTrackNumber = track.sourceTrackNumber,
    };

    for (const auto& command : track.commands) {
      std::visit(
          [&](const auto& typedCommand) {
            using TypedCommand = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<TypedCommand, VibratoCommand>) {
              observe(trackUsage.vibratoDepth, typedCommand.rawDepth, typedCommand.range);
            } else if constexpr (std::is_same_v<TypedCommand, TremoloCommand>) {
              observe(trackUsage.tremoloDepth, typedCommand.rawDepth, typedCommand.range);
            } else if constexpr (std::is_same_v<TypedCommand, ModulationRateCommand>) {
              observe(trackUsage.modulationRate, typedCommand.rawRate, typedCommand.range);
            }
          },
          command);
    }

    merge(result.vibratoDepth, trackUsage.vibratoDepth);
    merge(result.tremoloDepth, trackUsage.tremoloDepth);
    merge(result.modulationRate, trackUsage.modulationRate);
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
              observe(trackUsage.vibratoDepth, typedEvent.value, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, VibratoFrequency>) {
              observe(trackUsage.vibratoRate, typedEvent.value, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, TremoloDepth>) {
              observe(trackUsage.tremoloDepth, typedEvent.value, SourceRange{});
            } else if constexpr (std::is_same_v<TypedEvent, TremoloFrequency>) {
              observe(trackUsage.tremoloRate, typedEvent.value, SourceRange{});
            }
          },
          event);
    }

    merge(result.vibratoDepth, trackUsage.vibratoDepth);
    merge(result.vibratoRate, trackUsage.vibratoRate);
    merge(result.tremoloDepth, trackUsage.tremoloDepth);
    merge(result.tremoloRate, trackUsage.tremoloRate);
    result.tracks.push_back(std::move(trackUsage));
  }

  return result;
}

}  // namespace vgmtrans::core
