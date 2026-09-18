/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/PerformanceModel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>

namespace vgmtrans::core {

double effectivePitchBendSemitones(const PitchBendPerformanceEvent& bend, u16 sourceRangeCents,
                                   std::optional<u16> instrumentRangeCents) noexcept {
  if (!bend.normalizedWheelPosition) {
    return bend.semitones;
  }
  const double range = instrumentRangeCents.value_or(sourceRangeCents) / 100.0;
  return std::clamp(*bend.normalizedWheelPosition, -1.0, 1.0) * range;
}

const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event) {
  return std::visit([](const auto& typedEvent) -> const PerformanceEventHeader& { return typedEvent.header; }, event);
}

const PitchTransitionIntent* pitchTransitionIntent(const PerformanceAutomation& automation) {
  return std::get_if<PitchTransitionIntent>(&automation.intent);
}

PitchTransitionIntent* pitchTransitionIntent(PerformanceAutomation& automation) {
  return std::get_if<PitchTransitionIntent>(&automation.intent);
}

bool pitchTransitionContinuesVoice(const PerformanceAutomation& automation, const NotePerformanceEvent& note,
                                   const NotePerformanceEvent& previous) {
  const auto* transition = pitchTransitionIntent(automation);
  return transition != nullptr && transition->note == note.note && transition->previousNote == previous.note &&
         note.note != previous.note && previous.header.order() < note.header.order() && previous.lane == note.lane &&
         automation.realization.startTick <= note.header.tick &&
         (automation.realization.endReason == PerformanceAutomationEndReason::Completed ||
          automation.realization.endTick > automation.realization.startTick);
}

std::unordered_map<PerformanceNoteId, PerformanceNoteId> performanceNotePredecessors(const PerformanceTrack& track) {
  std::unordered_map<PerformanceNoteId, PerformanceNoteId> continued;
  if (track.automations.empty()) {
    return continued;
  }

  std::unordered_map<PerformanceNoteId, const NotePerformanceEvent*> notes;
  for (const auto& event : track.events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note != nullptr && note->note.valid()) {
      notes.try_emplace(note->note, note);
    }
  }
  for (const auto& automation : track.automations) {
    const auto* transition = pitchTransitionIntent(automation);
    if (transition == nullptr || !transition->previousNote) {
      continue;
    }
    const auto note = notes.find(transition->note);
    const auto previous = notes.find(*transition->previousNote);
    if (note != notes.end() && previous != notes.end() &&
        pitchTransitionContinuesVoice(automation, *note->second, *previous->second)) {
      continued.try_emplace(transition->note, *transition->previousNote);
    }
  }
  return continued;
}

double pitchTransitionValueAt(const PitchTransitionIntent& transition, u32 elapsedTicks) {
  const u32 duration = transition.timing.timelineTicks;
  const u32 clampedElapsed = std::min(elapsedTicks, duration);

  if (const auto* sampled = std::get_if<SampledAutomationCurve>(&transition.curve);
      sampled != nullptr && !sampled->samples.empty()) {
    const auto upper = std::ranges::upper_bound(sampled->samples, clampedElapsed, {}, &AutomationSample::tickOffset);
    return upper == sampled->samples.begin() ? sampled->samples.front().value : std::prev(upper)->value;
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
  for (const auto* tempo : orderedPerformanceEvents<TempoPerformanceEvent>(performance)) {
    if (points_.empty() || points_.back().microsecondsPerQuarter != tempo->microsecondsPerQuarter) {
      points_.push_back(Point{.tick = tempo->header.tick, .microsecondsPerQuarter = tempo->microsecondsPerQuarter});
    }
  }
  if (initialTempoMicrosecondsPerQuarter_ != 500000 && (points_.empty() || points_.front().tick != 0)) {
    points_.insert(points_.begin(), Point{.tick = 0, .microsecondsPerQuarter = initialTempoMicrosecondsPerQuarter_});
  }
}

u32 PerformanceTempoMap::microsecondsPerQuarterAt(u64 tick) const {
  const auto upper = std::ranges::upper_bound(points_, tick, {}, &Point::tick);
  return upper == points_.begin() ? initialTempoMicrosecondsPerQuarter_ : std::prev(upper)->microsecondsPerQuarter;
}

double PerformanceTempoMap::tickSeconds(u64 tick) const {
  return (static_cast<double>(microsecondsPerQuarterAt(tick)) / 1'000'000.0) /
         static_cast<double>(std::max<u32>(timebase_.ppqn, 1));
}

double PerformanceTempoMap::durationMilliseconds(u64 startTick, u32 durationTicks) const {
  if (durationTicks == 0) {
    return 0.0;
  }

  const u64 endTick = addTicks(startTick, durationTicks);
  const double ppqn = std::max<u32>(timebase_.ppqn, 1);
  u32 tempo = microsecondsPerQuarterAt(startTick);
  u64 cursor = startTick;
  double microseconds = 0.0;

  for (auto change = std::ranges::upper_bound(points_, startTick, {}, &Point::tick); change != points_.end();
       ++change) {
    if (change->tick >= endTick) {
      break;
    }
    microseconds += static_cast<double>(change->tick - cursor) * tempo / ppqn;
    cursor = change->tick;
    tempo = change->microsecondsPerQuarter;
  }
  microseconds += static_cast<double>(endTick - cursor) * tempo / ppqn;
  return microseconds / 1000.0;
}

u32 PerformanceTempoMap::durationTicksForMilliseconds(u64 startTick, double milliseconds) const {
  if (!(milliseconds > 0.0) || !std::isfinite(milliseconds)) {
    return 0;
  }
  const double ppqn = std::max<u32>(timebase_.ppqn, 1);
  double remainingMicroseconds = milliseconds * 1000.0;
  u32 tempo = microsecondsPerQuarterAt(startTick);
  u64 cursor = startTick;
  u64 elapsedTicks = 0;

  for (auto change = std::ranges::upper_bound(points_, startTick, {}, &Point::tick); change != points_.end();
       ++change) {
    const u64 segmentTicks = change->tick - cursor;
    const double segmentMicroseconds = static_cast<double>(segmentTicks) * tempo / ppqn;
    if (remainingMicroseconds <= segmentMicroseconds) {
      break;
    }
    remainingMicroseconds -= segmentMicroseconds;
    elapsedTicks += segmentTicks;
    if (elapsedTicks >= std::numeric_limits<u32>::max()) {
      return std::numeric_limits<u32>::max();
    }
    cursor = change->tick;
    tempo = change->microsecondsPerQuarter;
  }

  const double exactTailTicks = remainingMicroseconds * ppqn / std::max<u32>(tempo, 1);
  if (exactTailTicks >= std::numeric_limits<u32>::max() - elapsedTicks) {
    return std::numeric_limits<u32>::max();
  }
  const auto wholeTailTicks = static_cast<u64>(exactTailTicks);
  elapsedTicks += wholeTailTicks + (exactTailTicks - wholeTailTicks > 0.5 ? 1 : 0);
  return static_cast<u32>(std::min<u64>(elapsedTicks, std::numeric_limits<u32>::max()));
}

const PerformanceTrack* performanceTrackById(const PerformanceSequence& sequence, TrackId id) {
  const auto found =
      std::ranges::find_if(sequence.tracks, [id](const PerformanceTrack& track) { return track.id == id; });
  if (found == sequence.tracks.end()) {
    return nullptr;
  }
  return &*found;
}

std::vector<const PerformanceEvent*> performanceEventsForCommand(const PerformanceTrack& track,
                                                                 SourceCommandRef command) {
  std::vector<const PerformanceEvent*> events;
  for (const auto& event : track.events) {
    if (performanceEventHeader(event).sourceCommand == command) {
      events.push_back(&event);
    }
  }
  std::ranges::stable_sort(events, {},
                           [](const PerformanceEvent* event) { return performanceEventHeader(*event).order(); });
  return events;
}

}  // namespace vgmtrans::core
