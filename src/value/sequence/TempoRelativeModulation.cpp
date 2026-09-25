/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/TempoRelativeModulation.h"

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

struct EventRef {
  PerformanceEvent* event = nullptr;
  size_t trackIndex = 0;
};

[[nodiscard]] double tickSeconds(const PerformanceSequence& performance, u32 microsecondsPerQuarter) {
  return static_cast<double>(microsecondsPerQuarter) /
         (1'000'000.0 * static_cast<double>(std::max<u32>(performance.timebase.ppqn, 1)));
}

[[nodiscard]] double hertz(double cyclesPerTick, double secondsPerTick) {
  if (!std::isfinite(cyclesPerTick) || cyclesPerTick <= 0.0 || !std::isfinite(secondsPerTick) ||
      secondsPerTick <= 0.0) {
    return 0.0;
  }
  return cyclesPerTick / secondsPerTick;
}

[[nodiscard]] double delayMilliseconds(u32 ticks, double secondsPerTick) {
  if (!std::isfinite(secondsPerTick) || secondsPerTick <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(ticks) * secondsPerTick * 1000.0;
}

[[nodiscard]] PerformanceEventHeader derivedHeader(const TempoPerformanceEvent& tempo,
                                                   const PerformanceTrack& track) {
  return PerformanceEventHeader{
      .sourceAnnotation = tempo.header.sourceAnnotation,
      .track = track.id,
      .tick = tempo.header.tick,
      .sequence = tempo.header.sequence,
  };
}

void resolveContext(ModulationPerformanceEvent& event, double secondsPerTick) {
  if (event.context.cyclesPerTick) {
    event.context.frequencyHz = hertz(*event.context.cyclesPerTick, secondsPerTick);
  }
  if (event.context.delay && event.context.delay->tempoRelative) {
    event.context.delay->milliseconds = delayMilliseconds(event.context.delay->ticks, secondsPerTick);
  }
}

[[nodiscard]] bool isTempoRelative(const PerformanceEvent& event) {
  if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
    return modulation->context.cyclesPerTick.has_value() ||
           (modulation->context.delay && modulation->context.delay->tempoRelative);
  }
  return false;
}

[[nodiscard]] bool belongsInTimeline(const PerformanceEvent& event) {
  return std::holds_alternative<TempoPerformanceEvent>(event) ||
         std::holds_alternative<ModulationPerformanceEvent>(event);
}

}  // namespace

void resolveTempoRelativeModulation(PerformanceSequence& performance) {
  const bool hasTempoRelativeModulation = std::ranges::any_of(performance.tracks, [](const PerformanceTrack& track) {
    return std::ranges::any_of(track.events, isTempoRelative);
  });
  if (!hasTempoRelativeModulation) {
    return;
  }

  std::vector<EventRef> timeline;
  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    auto& track = performance.tracks[trackIndex];
    for (auto& event : track.events) {
      if (belongsInTimeline(event)) {
        timeline.push_back(EventRef{.event = &event, .trackIndex = trackIndex});
      }
    }
  }
  std::ranges::stable_sort(timeline, {},
                           [](const EventRef& ref) { return performanceEventHeader(*ref.event).order(); });

  // Rates and delays are independent controls. Keep source events by target
  // and pitch layer; their storage stays stable until derived events are appended.
  using ActiveModulation = std::map<std::pair<ModulationPerformanceTarget, u32>, const ModulationPerformanceEvent*>;
  std::vector<ActiveModulation> states(performance.tracks.size());
  std::vector<std::vector<PerformanceEvent>> derived(performance.tracks.size());
  u32 currentTempo = performance.initialTempoMicrosecondsPerQuarter;
  double secondsPerTick = tickSeconds(performance, currentTempo);

  for (const EventRef& ref : timeline) {
    auto& event = *ref.event;
    auto& state = states[ref.trackIndex];

    if (auto* tempo = std::get_if<TempoPerformanceEvent>(&event)) {
      if (tempo->microsecondsPerQuarter == currentTempo) {
        continue;
      }
      currentTempo = tempo->microsecondsPerQuarter;
      secondsPerTick = tickSeconds(performance, currentTempo);

      for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
        auto& track = performance.tracks[trackIndex];
        if (tempo->header.tick > track.endTick && track.endTick != 0) {
          continue;
        }
        for (const auto& [key, modulation] : states[trackIndex]) {
          auto update = *modulation;
          update.header = derivedHeader(*tempo, track);
          resolveContext(update, secondsPerTick);
          derived[trackIndex].emplace_back(std::move(update));
        }
      }
      continue;
    }

    auto& modulation = std::get<ModulationPerformanceEvent>(event);
    resolveContext(modulation, secondsPerTick);
    const auto target = modulation.target;
    const bool rate = target == ModulationPerformanceTarget::VibratoRate ||
                      target == ModulationPerformanceTarget::TremoloRate ||
                      target == ModulationPerformanceTarget::PanRate;
    const bool delay =
        target == ModulationPerformanceTarget::VibratoDelay || target == ModulationPerformanceTarget::TremoloDelay;
    if (rate || delay) {
      const bool vibrato =
          target == ModulationPerformanceTarget::VibratoRate || target == ModulationPerformanceTarget::VibratoDelay;
      const auto key = std::pair{target, vibrato ? modulation.pitchLayer.value : 0};
      const bool relative = rate ? modulation.context.cyclesPerTick.has_value()
                                 : modulation.context.delay && modulation.context.delay->tempoRelative;
      if (relative) {
        state.insert_or_assign(key, &modulation);
      } else {
        state.erase(key);
      }
    }
  }

  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    auto& track = performance.tracks[trackIndex];
    auto& additions = derived[trackIndex];
    if (additions.empty()) {
      continue;
    }
    track.events.insert(track.events.end(), std::make_move_iterator(additions.begin()),
                        std::make_move_iterator(additions.end()));
    std::ranges::stable_sort(track.events, {},
                             [](const PerformanceEvent& event) { return performanceEventHeader(event).order(); });
  }
}

}  // namespace vgmtrans::core
