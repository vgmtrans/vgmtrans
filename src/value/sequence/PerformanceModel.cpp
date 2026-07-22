/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <tuple>

namespace vgmtrans::core {

const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event) {
  return std::visit([](const auto& typedEvent) -> const PerformanceEventHeader& { return typedEvent.header; }, event);
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
    for (const auto& event : automation.points) {
      events.push_back(&event);
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
