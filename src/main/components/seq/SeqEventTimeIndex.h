#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

class SeqEvent;

struct SeqTimedEvent {
  uint32_t startTick;
  uint32_t duration;
  SeqEvent* event;

  [[nodiscard]] uint32_t endTickExclusive() const noexcept {
    return startTick + (duration == 0 ? 1u : duration);
  }

  [[nodiscard]] bool isActiveAt(uint32_t tick) const noexcept {
    return startTick <= tick && tick < endTickExclusive();
  }

  [[nodiscard]] bool overlapsRange(uint32_t rangeStart, uint32_t rangeEnd) const noexcept {
    if (duration == 0) {
      return startTick >= rangeStart && startTick <= rangeEnd;
    }
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

  Index addEvent(std::unique_ptr<SeqEvent> event, uint32_t startTick, uint32_t duration);
  void clear();
  void finalize();

  [[nodiscard]] bool finalized() const noexcept { return m_finalized; }
  [[nodiscard]] size_t size() const noexcept { return m_events.size(); }

  [[nodiscard]] const SeqTimedEvent& event(Index idx) const { return m_events.at(idx); }
  [[nodiscard]] SeqTimedEvent& event(Index idx) { return m_events.at(idx); }
  [[nodiscard]] uint32_t endTickExclusive(Index idx) const { return m_events.at(idx).endTickExclusive(); }

  // Convenience queries. For repeated sequential queries, prefer Cursor.
  void getActiveAt(uint32_t tick, std::vector<const SeqTimedEvent*>& out) const;
  void getActiveInRange(uint32_t startTick, uint32_t endTick, std::vector<const SeqTimedEvent*>& out) const;

  class Cursor {
   public:
    explicit Cursor(const SeqEventTimeIndex& index);

    void reset(uint32_t tick = 0);
    void seek(uint32_t tick);
    void getActiveAt(uint32_t tick, std::vector<const SeqTimedEvent*>& out);
    void getActiveInRange(uint32_t startTick, uint32_t endTick, std::vector<const SeqTimedEvent*>& out);

   private:
    void rebuildActiveIndex();
    void advanceTo(uint32_t tick);
    void addActive(Index idx);
    void removeActive(Index idx);

    const SeqEventTimeIndex* m_index = nullptr;
    size_t m_nextStart = 0;
    size_t m_nextEnd = 0;
    uint32_t m_currentTick = 0;
    std::vector<Index> m_active;
    std::vector<int32_t> m_activePositions;
  };

 private:
  std::vector<SeqTimedEvent> m_events;
  std::vector<std::unique_ptr<SeqEvent>> m_ownedEvents;
  std::vector<Index> m_byStart;
  std::vector<Index> m_byEnd;
  bool m_finalized = false;
};
