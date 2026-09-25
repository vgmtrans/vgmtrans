/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

#include <variant>
#include <vector>

// Views borrow events from the track; keep the rendered performance alive.
template <class Event>
std::vector<const Event*> eventsOfType(const vgmtrans::core::PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const auto& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}
