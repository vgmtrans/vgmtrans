/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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
    return previous.value;
  }

  if (duration == 0) {
    return transition.targetKey;
  }
  const double position = static_cast<double>(clampedElapsed) / static_cast<double>(duration);
  return transition.startKey + ((transition.targetKey - transition.startKey) * position);
}

PerformanceTempoMap::PerformanceTempoMap(const PerformanceSequence& performance)
    : timebase_(performance.timebase),
      initialTempoMicrosecondsPerQuarter_(performance.initialTempoMicrosecondsPerQuarter) {
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      const auto* tempo = std::get_if<TempoPerformanceEvent>(&event);
      if (tempo == nullptr) {
        continue;
      }
      changes_.push_back(Change{
          .tick = tempo->header.tick,
          .microsecondsPerQuarter = tempo->microsecondsPerQuarter,
          .track = tempo->header.track,
          .sequence = tempo->header.sequence,
          .order = changes_.size(),
      });
    }
  }
  std::ranges::stable_sort(changes_, [](const Change& lhs, const Change& rhs) {
    return std::tie(lhs.tick, lhs.sequence, lhs.order) < std::tie(rhs.tick, rhs.sequence, rhs.order);
  });
  std::optional<u32> currentTempo;
  std::erase_if(changes_, [&](const Change& change) {
    if (currentTempo && *currentTempo == change.microsecondsPerQuarter) {
      return true;
    }
    currentTempo = change.microsecondsPerQuarter;
    return false;
  });
}

u32 PerformanceTempoMap::microsecondsPerQuarterAt(u64 tick) const {
  u32 microsecondsPerQuarter = initialTempoMicrosecondsPerQuarter_;
  for (const auto& change : changes_) {
    if (change.tick > tick) {
      break;
    }
    microsecondsPerQuarter = change.microsecondsPerQuarter;
  }
  return microsecondsPerQuarter;
}

double PerformanceTempoMap::tickSeconds(u64 tick) const {
  return (static_cast<double>(microsecondsPerQuarterAt(tick)) / 1'000'000.0) /
         static_cast<double>(std::max<u16>(timebase_.ppqn, 1));
}

double PerformanceTempoMap::durationMilliseconds(u64 startTick, u32 durationTicks) const {
  if (durationTicks == 0) {
    return 0.0;
  }

  const u64 endTick = startTick > std::numeric_limits<u64>::max() - durationTicks ? std::numeric_limits<u64>::max()
                                                                                  : startTick + durationTicks;
  const double ppqn = std::max<u16>(timebase_.ppqn, 1);
  u32 tempo = initialTempoMicrosecondsPerQuarter_;
  u64 cursor = startTick;
  double microseconds = 0.0;

  for (const auto& change : changes_) {
    if (change.tick <= startTick) {
      tempo = change.microsecondsPerQuarter;
      continue;
    }
    if (change.tick >= endTick) {
      break;
    }
    microseconds += static_cast<double>(change.tick - cursor) * tempo / ppqn;
    cursor = change.tick;
    tempo = change.microsecondsPerQuarter;
  }
  microseconds += static_cast<double>(endTick - cursor) * tempo / ppqn;
  return microseconds / 1000.0;
}

u32 PerformanceTempoMap::durationTicksForMilliseconds(u64 startTick, double milliseconds) const {
  if (!(milliseconds > 0.0) || !std::isfinite(milliseconds)) {
    return 0;
  }
  const double targetSeconds = milliseconds / 1000.0;
  double elapsedSeconds = 0.0;
  for (u32 ticks = 0; ticks < std::numeric_limits<u32>::max(); ++ticks) {
    const double nextSeconds = elapsedSeconds + tickSeconds(startTick + ticks);
    if (nextSeconds >= targetSeconds) {
      return targetSeconds - elapsedSeconds <= nextSeconds - targetSeconds ? ticks : ticks + 1;
    }
    elapsedSeconds = nextSeconds;
  }
  return std::numeric_limits<u32>::max();
}

bool PerformanceTempoMap::contains(const TempoPerformanceEvent& event) const {
  return std::ranges::any_of(changes_, [&](const Change& change) {
    return change.tick == event.header.tick && change.track == event.header.track &&
           change.sequence == event.header.sequence && change.microsecondsPerQuarter == event.microsecondsPerQuarter;
  });
}

std::vector<PerformanceTempoMap::Point> PerformanceTempoMap::points() const {
  std::vector<Point> result;
  result.reserve(changes_.size() + 1);
  if (initialTempoMicrosecondsPerQuarter_ != 500000 && (changes_.empty() || changes_.front().tick != 0)) {
    result.push_back(Point{
        .tick = 0,
        .microsecondsPerQuarter = initialTempoMicrosecondsPerQuarter_,
    });
  }
  for (const Change& change : changes_) {
    result.push_back(Point{
        .tick = change.tick,
        .microsecondsPerQuarter = change.microsecondsPerQuarter,
    });
  }
  return result;
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
  if (!header.track.valid() || header.track.value >= program.tracks.size()) {
    return nullptr;
  }
  return program.tracks[header.track.value].command(header.sourceCommand);
}

std::vector<const PerformanceEvent*> performanceEventsForCommand(const PerformanceTrack& track, CommandId command) {
  std::vector<const PerformanceEvent*> events;
  for (const auto& event : track.events) {
    if (performanceEventHeader(event).sourceCommand == command) {
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

}  // namespace vgmtrans::core
