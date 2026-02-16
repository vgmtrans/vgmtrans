/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "TransposeTimeline.h"

#include <algorithm>
#include <numeric>

#include "SeqEvent.h"
#include "SeqTrack.h"

void TransposeTimeline::clear() {
  m_totalByIndex.clear();
  m_totalByTimedEvent.clear();
  m_globalChanges.clear();
  m_trackChanges.clear();
}

void TransposeTimeline::build(const SeqEventTimeIndex& timedEvents) {
  clear();

  const size_t eventCount = timedEvents.size();
  if (eventCount == 0) {
    return;
  }

  m_totalByIndex.assign(eventCount, 0);
  m_totalByTimedEvent.reserve(eventCount * 2 + 1);

  std::vector<SeqEventTimeIndex::Index> order(eventCount);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&timedEvents](SeqEventTimeIndex::Index a, SeqEventTimeIndex::Index b) {
    const auto& eventA = timedEvents.event(a);
    const auto& eventB = timedEvents.event(b);
    if (eventA.startTick != eventB.startTick) {
      return eventA.startTick < eventB.startTick;
    }
    return a < b;
  });

  int globalTranspose = 0;
  std::unordered_map<const SeqTrack*, int> trackTranspose;
  trackTranspose.reserve(32);

  for (SeqEventTimeIndex::Index idx : order) {
    const SeqTimedEvent& timed = timedEvents.event(idx);
    const auto* event = timed.event;
    if (!event) {
      continue;
    }

    if (const auto* transposeEvent = dynamic_cast<const TransposeSeqEvent*>(event)) {
      const int transposeValue = transposeEvent->transpose();
      if (transposeEvent->scope() == TransposeSeqEvent::Scope::Global) {
        globalTranspose = transposeValue;
        appendChange(m_globalChanges, timed.startTick, transposeValue);
      } else if (const auto* track = event->parentTrack) {
        trackTranspose[track] = transposeValue;
        appendChange(m_trackChanges[track], timed.startTick, transposeValue);
      }
    }

    int totalTranspose = globalTranspose;
    if (const auto* track = event->parentTrack) {
      auto trackIt = trackTranspose.find(track);
      if (trackIt != trackTranspose.end()) {
        totalTranspose += trackIt->second;
      }
    }
    m_totalByIndex[idx] = totalTranspose;
    m_totalByTimedEvent.emplace(&timed, totalTranspose);
  }
}

int TransposeTimeline::totalTransposeForTimedEvent(SeqEventTimeIndex::Index index) const noexcept {
  if (index >= m_totalByIndex.size()) {
    return 0;
  }
  return m_totalByIndex[index];
}

int TransposeTimeline::totalTransposeForTimedEvent(const SeqTimedEvent* timedEvent) const noexcept {
  if (!timedEvent) {
    return 0;
  }
  auto it = m_totalByTimedEvent.find(timedEvent);
  if (it == m_totalByTimedEvent.end()) {
    return 0;
  }
  return it->second;
}

int TransposeTimeline::totalTransposeAtTick(const SeqTrack* track, uint32_t tick) const noexcept {
  const int global = valueAtTick(m_globalChanges, tick);
  if (!track) {
    return global;
  }
  auto it = m_trackChanges.find(track);
  if (it == m_trackChanges.end()) {
    return global;
  }
  return global + valueAtTick(it->second, tick);
}

void TransposeTimeline::appendChange(std::vector<ChangePoint>& changes, uint32_t tick, int value) {
  if (!changes.empty() && changes.back().tick == tick) {
    changes.back().value = value;
    return;
  }
  if (!changes.empty() && changes.back().value == value) {
    return;
  }
  changes.push_back({tick, value});
}

int TransposeTimeline::valueAtTick(const std::vector<ChangePoint>& changes, uint32_t tick) noexcept {
  if (changes.empty()) {
    return 0;
  }

  auto it = std::upper_bound(changes.begin(), changes.end(), tick,
                             [](uint32_t value, const ChangePoint& point) {
                               return value < point.tick;
                             });
  if (it == changes.begin()) {
    return 0;
  }
  return std::prev(it)->value;
}
