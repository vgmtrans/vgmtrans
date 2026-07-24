/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace vgmtrans::core {

const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event) {
  return std::visit([](const auto& typedEvent) -> const PerformanceEventHeader& { return typedEvent.header; }, event);
}

const PitchTransitionIntent* pitchTransitionIntent(const PerformanceAutomation& automation) {
  return std::get_if<PitchTransitionIntent>(&automation.intent);
}

PitchTransitionIntent* pitchTransitionIntent(PerformanceAutomation& automation) {
  return std::get_if<PitchTransitionIntent>(&automation.intent);
}

double pitchTransitionValueAt(const PitchTransitionIntent& transition, u32 elapsedTicks) {
  const u32 duration = transition.timing.timelineTicks;
  const u32 clampedElapsed = std::min(elapsedTicks, duration);

  if (const auto* sampled = std::get_if<SampledAutomationCurve>(&transition.curve);
      sampled != nullptr && !sampled->samples.empty()) {
    const auto upper = std::ranges::upper_bound(sampled->samples, clampedElapsed, {}, &AutomationSample::tickOffset);
    if (upper == sampled->samples.begin()) {
      return sampled->samples.front().value;
    }
    if (upper == sampled->samples.end()) {
      return sampled->samples.back().value;
    }

    const auto& previous = *std::prev(upper);
    if (sampled->interpolation == AutomationSampleInterpolation::Step || upper->tickOffset == previous.tickOffset) {
      return previous.value;
    }
    const double position = static_cast<double>(clampedElapsed - previous.tickOffset) /
                            static_cast<double>(upper->tickOffset - previous.tickOffset);
    return previous.value + ((upper->value - previous.value) * position);
  }

  if (duration == 0) {
    return transition.targetKey;
  }
  const double position = static_cast<double>(clampedElapsed) / static_cast<double>(duration);
  return transition.startKey + ((transition.targetKey - transition.startKey) * position);
}

std::vector<const PerformanceEvent*> flattenedPerformanceEvents(const PerformanceTrack& track) {
  std::vector<const PerformanceEvent*> events;
  size_t eventCount = track.events.size();
  for (const auto& automation : track.automations) {
    eventCount += automation.points.size();
  }
  events.reserve(eventCount);
  for (const auto& event : track.events) {
    events.push_back(&event);
  }
  for (const auto& automation : track.automations) {
    for (const auto& point : automation.points) {
      events.push_back(&point);
    }
  }
  std::ranges::stable_sort(events, [](const PerformanceEvent* lhs, const PerformanceEvent* rhs) {
    const auto& lhsHeader = performanceEventHeader(*lhs);
    const auto& rhsHeader = performanceEventHeader(*rhs);
    return std::tie(lhsHeader.tick, lhsHeader.sequence) < std::tie(rhsHeader.tick, rhsHeader.sequence);
  });
  return events;
}

const PerformanceTrack* performanceTrackById(const PerformanceSequence& sequence, TrackId id) {
  const auto found =
      std::ranges::find_if(sequence.tracks, [id](const PerformanceTrack& track) { return track.id == id; });
  if (found == sequence.tracks.end()) {
    return nullptr;
  }
  return &*found;
}

const SourceCommand* sourceCommandForEvent(const SequenceProgram& program, const PerformanceEventHeader& header) {
  const TrackProgram* track = trackById(program, header.track);
  if (track == nullptr) {
    return nullptr;
  }
  return sourceCommandById(*track, header.sourceCommand);
}

std::vector<const PerformanceEvent*> performanceEventsForCommand(const PerformanceTrack& track, CommandId command) {
  std::vector<const PerformanceEvent*> events;
  const auto appendMatching = [&](const auto& candidates) {
    for (const auto& event : candidates) {
      if (performanceEventHeader(event).sourceCommand == command) {
        events.push_back(&event);
      }
    }
  };
  appendMatching(track.events);
  for (const auto& automation : track.automations) {
    appendMatching(automation.points);
  }
  std::ranges::stable_sort(events, [](const PerformanceEvent* lhs, const PerformanceEvent* rhs) {
    const auto& lhsHeader = performanceEventHeader(*lhs);
    const auto& rhsHeader = performanceEventHeader(*rhs);
    return std::tie(lhsHeader.tick, lhsHeader.sequence) < std::tie(rhsHeader.tick, rhsHeader.sequence);
  });
  return events;
}

}  // namespace vgmtrans::core
