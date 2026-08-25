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
#include <optional>
#include <tuple>
#include <vector>

namespace vgmtrans::core {

namespace {

struct EventRef {
  PerformanceEvent* event = nullptr;
  size_t trackIndex = 0;
  size_t eventIndex = 0;
};

struct TrackModulationState {
  std::optional<ModulationPerformanceEvent> vibratoRate;
  std::optional<ModulationPerformanceEvent> tremoloRate;
  std::optional<ModulationPerformanceEvent> panRate;
  std::optional<u32> vibratoDelayTicks;
  std::optional<u32> tremoloDelayTicks;
  LfoDelayUpdateMode vibratoDelayUpdateMode = LfoDelayUpdateMode::CurrentAndFutureNotes;
  LfoDelayUpdateMode tremoloDelayUpdateMode = LfoDelayUpdateMode::CurrentAndFutureNotes;
};

[[nodiscard]] double tickSeconds(const PerformanceSequence& performance, u32 microsecondsPerQuarter) {
  return static_cast<double>(microsecondsPerQuarter) /
         (1'000'000.0 * static_cast<double>(std::max<u16>(performance.timebase.ppqn, 1)));
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

[[nodiscard]] std::optional<ModulationPerformanceEvent>* relativeRate(
    TrackModulationState& state,
    ModulationPerformanceTarget target) {
  switch (target) {
    case ModulationPerformanceTarget::VibratoRate:
      return &state.vibratoRate;
    case ModulationPerformanceTarget::TremoloRate:
      return &state.tremoloRate;
    case ModulationPerformanceTarget::PanRate:
      return &state.panRate;
    case ModulationPerformanceTarget::VibratoDepth:
    case ModulationPerformanceTarget::TremoloDepth:
    case ModulationPerformanceTarget::PanDepth:
      return nullptr;
  }
  return nullptr;
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
  if (event.context.delayIsTempoRelative && event.context.delayTicks) {
    event.context.delayMilliseconds = delayMilliseconds(*event.context.delayTicks, secondsPerTick);
  }
}

[[nodiscard]] bool isTempoRelative(const PerformanceEvent& event) {
  if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
    return modulation->context.cyclesPerTick.has_value() || modulation->context.delayIsTempoRelative;
  }
  if (const auto* delay = std::get_if<VibratoDelayPerformanceEvent>(&event)) {
    return delay->tempoRelative;
  }
  if (const auto* delay = std::get_if<TremoloDelayPerformanceEvent>(&event)) {
    return delay->tempoRelative;
  }
  return false;
}

[[nodiscard]] bool belongsInTimeline(const PerformanceEvent& event) {
  return std::holds_alternative<TempoPerformanceEvent>(event) ||
         std::holds_alternative<ModulationPerformanceEvent>(event) ||
         std::holds_alternative<VibratoDelayPerformanceEvent>(event) ||
         std::holds_alternative<TremoloDelayPerformanceEvent>(event);
}

}  // namespace

void resolveTempoRelativeModulation(PerformanceSequence& performance) {
  if (!std::ranges::any_of(performance.tracks, &PerformanceTrack::hasPhysicalModulation)) {
    return;
  }
  const bool hasTempoRelativeModulation = std::ranges::any_of(performance.tracks, [](const PerformanceTrack& track) {
    return track.hasPhysicalModulation && std::ranges::any_of(track.events, isTempoRelative);
  });
  if (!hasTempoRelativeModulation) {
    return;
  }

  std::vector<EventRef> timeline;
  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    auto& track = performance.tracks[trackIndex];
    for (size_t eventIndex = 0; eventIndex < track.events.size(); ++eventIndex) {
      if (!belongsInTimeline(track.events[eventIndex])) {
        continue;
      }
      timeline.push_back(EventRef{
          .event = &track.events[eventIndex],
          .trackIndex = trackIndex,
          .eventIndex = eventIndex,
      });
    }
  }
  std::ranges::stable_sort(timeline, [](const EventRef& lhs, const EventRef& rhs) {
    const auto& left = performanceEventHeader(*lhs.event);
    const auto& right = performanceEventHeader(*rhs.event);
    return std::tie(left.tick, left.sequence, lhs.trackIndex, lhs.eventIndex) <
           std::tie(right.tick, right.sequence, rhs.trackIndex, rhs.eventIndex);
  });

  std::vector<TrackModulationState> states(performance.tracks.size());
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
        auto& trackState = states[trackIndex];
        const auto appendRate = [&](const std::optional<ModulationPerformanceEvent>& rate) {
          if (!rate || !rate->context.cyclesPerTick) {
            return;
          }
          auto& update = std::get<ModulationPerformanceEvent>(derived[trackIndex].emplace_back(*rate));
          update.header = derivedHeader(*tempo, track);
          resolveContext(update, secondsPerTick);
        };
        appendRate(trackState.vibratoRate);
        appendRate(trackState.tremoloRate);
        appendRate(trackState.panRate);

        if (trackState.vibratoDelayTicks) {
          derived[trackIndex].emplace_back(VibratoDelayPerformanceEvent{
              .header = derivedHeader(*tempo, track),
              .delayTicks = *trackState.vibratoDelayTicks,
              .milliseconds = delayMilliseconds(*trackState.vibratoDelayTicks, secondsPerTick),
              .tempoRelative = true,
              .updateMode = trackState.vibratoDelayUpdateMode,
          });
        }
        if (trackState.tremoloDelayTicks) {
          derived[trackIndex].emplace_back(TremoloDelayPerformanceEvent{
              .header = derivedHeader(*tempo, track),
              .delayTicks = *trackState.tremoloDelayTicks,
              .milliseconds = delayMilliseconds(*trackState.tremoloDelayTicks, secondsPerTick),
              .tempoRelative = true,
              .updateMode = trackState.tremoloDelayUpdateMode,
          });
        }
      }
      continue;
    }

    if (auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
      resolveContext(*modulation, secondsPerTick);
      if (auto* rate = relativeRate(state, modulation->target)) {
        if (modulation->context.cyclesPerTick) {
          *rate = *modulation;
        } else {
          rate->reset();
        }
      }
      continue;
    }

    if (auto* delay = std::get_if<VibratoDelayPerformanceEvent>(&event)) {
      if (delay->tempoRelative) {
        delay->milliseconds = delayMilliseconds(delay->delayTicks, secondsPerTick);
        state.vibratoDelayTicks = delay->delayTicks;
        state.vibratoDelayUpdateMode = delay->updateMode;
      } else {
        state.vibratoDelayTicks.reset();
      }
      continue;
    }

    if (auto* delay = std::get_if<TremoloDelayPerformanceEvent>(&event)) {
      if (delay->tempoRelative) {
        delay->milliseconds = delayMilliseconds(delay->delayTicks, secondsPerTick);
        state.tremoloDelayTicks = delay->delayTicks;
        state.tremoloDelayUpdateMode = delay->updateMode;
      } else {
        state.tremoloDelayTicks.reset();
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
    std::ranges::stable_sort(track.events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
      const auto& left = performanceEventHeader(lhs);
      const auto& right = performanceEventHeader(rhs);
      return std::tie(left.tick, left.sequence) < std::tie(right.tick, right.sequence);
    });
  }
}

}  // namespace vgmtrans::core
