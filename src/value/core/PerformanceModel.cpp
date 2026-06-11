/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/PerformanceModel.h"

#include <algorithm>

namespace vgmtrans::core {

const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event) {
  return std::visit([](const auto& typedEvent) -> const PerformanceEventHeader& { return typedEvent.header; }, event);
}

const PerformanceTrack* performanceTrackById(const PerformanceSequence& sequence, TrackId id) {
  const auto found = std::ranges::find_if(sequence.tracks, [id](const PerformanceTrack& track) {
    return track.id == id;
  });
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
  for (const auto& event : track.events) {
    if (performanceEventHeader(event).sourceCommand == command) {
      events.push_back(&event);
    }
  }
  return events;
}

}  // namespace vgmtrans::core
