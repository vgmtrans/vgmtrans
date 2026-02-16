/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "SeqEventTimeIndex.h"

class SeqTrack;

class TransposeTimeline {
 public:
  void clear();
  void build(const SeqEventTimeIndex& timedEvents);

  [[nodiscard]] bool empty() const noexcept { return m_totalByIndex.empty(); }
  [[nodiscard]] size_t size() const noexcept { return m_totalByIndex.size(); }

  [[nodiscard]] int totalTransposeForTimedEvent(SeqEventTimeIndex::Index index) const noexcept;
  [[nodiscard]] int totalTransposeForTimedEvent(const SeqTimedEvent* timedEvent) const noexcept;
  [[nodiscard]] int totalTransposeAtTick(const SeqTrack* track, uint32_t tick) const noexcept;

 private:
  struct ChangePoint {
    uint32_t tick = 0;
    int value = 0;
  };

  static void appendChange(std::vector<ChangePoint>& changes, uint32_t tick, int value);
  [[nodiscard]] static int valueAtTick(const std::vector<ChangePoint>& changes, uint32_t tick) noexcept;

  std::vector<int> m_totalByIndex;
  std::unordered_map<const SeqTimedEvent*, int> m_totalByTimedEvent;
  std::vector<ChangePoint> m_globalChanges;
  std::unordered_map<const SeqTrack*, std::vector<ChangePoint>> m_trackChanges;
};
