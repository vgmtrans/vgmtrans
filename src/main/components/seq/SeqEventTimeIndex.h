/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>
#include "base/Types.h"

class SeqEvent;

struct SeqTimedEvent {
  u32 startTick;
  u32 duration;
  SeqEvent* event;

  // Zero duration is treated as a 1-tick event for range math
  [[nodiscard]] u32 endTickExclusive() const noexcept {
    return startTick + (duration == 0 ? 1u : duration);
  }

  [[nodiscard]] bool isActiveAt(u32 tick) const noexcept {
    return startTick <= tick && tick < endTickExclusive();
  }

  [[nodiscard]] bool overlapsRange(u32 rangeStart, u32 rangeEnd) const noexcept {
    return startTick <= rangeEnd && endTickExclusive() > rangeStart;
  }
};

class SeqEventTimeIndex {
 public:
  using Index = size_t;
  static constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

  SeqEventTimeIndex() = default;
  ~SeqEventTimeIndex();

  SeqEventTimeIndex(const SeqEventTimeIndex&) = delete;
  SeqEventTimeIndex& operator=(const SeqEventTimeIndex&) = delete;
  SeqEventTimeIndex(SeqEventTimeIndex&&) = default;
  SeqEventTimeIndex& operator=(SeqEventTimeIndex&&) = default;

  Index addEvent(SeqEvent* event, u32 startTick, u32 duration);
  void clear();     // clears all events and data structures

  // finalize() builds the data structures needed for Cursor::advanceTo(). It should be called
  // after all events have been added.
  void finalize();

  [[nodiscard]] bool finalized() const noexcept { return m_finalized; }
  [[nodiscard]] size_t size() const noexcept { return m_events.size(); }

  [[nodiscard]] const SeqTimedEvent& event(Index idx) const { return m_events.at(idx); }
  [[nodiscard]] SeqTimedEvent& event(Index idx) { return m_events.at(idx); }
  [[nodiscard]] u32 endTickExclusive(Index idx) const { return m_events.at(idx).endTickExclusive(); }
  [[nodiscard]] bool firstStartTick(const SeqEvent* event, u32& outTick) const noexcept;

  // Convenience queries. For repeated sequential queries, prefer Cursor
  void getActiveAt(u32 tick, std::vector<const SeqTimedEvent*>& out) const;
  void getActiveInRange(u32 startTick, u32 endTick, std::vector<const SeqTimedEvent*>& out) const;

  class Cursor {
   public:
    explicit Cursor(const SeqEventTimeIndex& index);

    void reset(u32 tick = 0);
    void seek(u32 tick);
    void getActiveAt(u32 tick, std::vector<const SeqTimedEvent*>& out);
    void getActiveInRange(u32 startTick, u32 endTick, std::vector<const SeqTimedEvent*>& out);

   private:
    void rebuildActiveIndex();
    void advanceTo(u32 tick);
    void addActive(Index idx);
    void removeActive(Index idx);
    [[nodiscard]] bool isReady() const noexcept;

    const SeqEventTimeIndex* m_index = nullptr;
    size_t m_nextStart = 0;  // Next candidate in m_byStart
    size_t m_nextEnd = 0;    // Next candidate in m_byEnd
    u32 m_currentTick = 0;

    std::vector<Index> m_active;  // the compact list of currently active event indices

    // m_activePositions is a reverse index: for each event index idx, it stores the position of
    // idx in m_active, or -1 if the event is not active.
    std::vector<s32> m_activePositions;
  };

 private:
  std::vector<SeqTimedEvent> m_events;
  std::vector<Index> m_byStart;  // Event indices sorted by start tick
  std::vector<Index> m_byEnd;    // Event indices sorted by end tick
  // m_firstStart maps the earliest start time per event. Events may run multiple times due to
  // looping, so we only concern ourselves with the first time of execution for simplicity.
  std::unordered_map<const SeqEvent*, u32> m_firstStart;
  bool m_finalized = false;
};
