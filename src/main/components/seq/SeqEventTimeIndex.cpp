#include "SeqEventTimeIndex.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "SeqEvent.h"

SeqEventTimeIndex::~SeqEventTimeIndex() = default;

SeqEventTimeIndex::Index SeqEventTimeIndex::addEvent(SeqEvent* event,
                                                     uint32_t startTick,
                                                     uint32_t duration) {
  m_events.push_back(SeqTimedEvent{startTick, duration, event});
  m_finalized = false;
  return m_events.size() - 1;
}

void SeqEventTimeIndex::clear() {
  m_events.clear();
  m_byStart.clear();
  m_byEnd.clear();
  m_finalized = false;
}

void SeqEventTimeIndex::finalize() {
  const size_t count = m_events.size();
  m_byStart.resize(count);
  m_byEnd.resize(count);
  std::iota(m_byStart.begin(), m_byStart.end(), 0);
  std::iota(m_byEnd.begin(), m_byEnd.end(), 0);

  std::sort(m_byStart.begin(), m_byStart.end(), [this](Index a, Index b) {
    const auto& evA = m_events[a];
    const auto& evB = m_events[b];
    if (evA.startTick != evB.startTick) {
      return evA.startTick < evB.startTick;
    }
    return a < b;
  });

  std::sort(m_byEnd.begin(), m_byEnd.end(), [this](Index a, Index b) {
    const auto endA = m_events[a].endTickExclusive();
    const auto endB = m_events[b].endTickExclusive();
    if (endA != endB) {
      return endA < endB;
    }
    return a < b;
  });

  m_finalized = true;
}

void SeqEventTimeIndex::getActiveAt(uint32_t tick, std::vector<const SeqTimedEvent*>& out) const {
  Cursor cursor(*this);
  cursor.getActiveAt(tick, out);
}

void SeqEventTimeIndex::getActiveInRange(uint32_t startTick,
                                         uint32_t endTick,
                                         std::vector<const SeqTimedEvent*>& out) const {
  Cursor cursor(*this);
  cursor.getActiveInRange(startTick, endTick, out);
}

SeqEventTimeIndex::Cursor::Cursor(const SeqEventTimeIndex& index) : m_index(&index) {
  reset(0);
}

void SeqEventTimeIndex::Cursor::reset(uint32_t tick) {
  m_nextStart = 0;
  m_nextEnd = 0;
  m_currentTick = 0;
  rebuildActiveIndex();
  if (m_index != nullptr && m_index->finalized()) {
    advanceTo(tick);
  }
}

void SeqEventTimeIndex::Cursor::seek(uint32_t tick) {
  if (m_index == nullptr || !m_index->finalized()) {
    return;
  }

  if (tick < m_currentTick) {
    reset(tick);
    return;
  }

  advanceTo(tick);
}

void SeqEventTimeIndex::Cursor::getActiveAt(uint32_t tick, std::vector<const SeqTimedEvent*>& out) {
  out.clear();
  if (m_index == nullptr || !m_index->finalized()) {
    return;
  }

  seek(tick);
  out.reserve(m_active.size());
  for (Index idx : m_active) {
    out.push_back(&m_index->event(idx));
  }
}

void SeqEventTimeIndex::Cursor::getActiveInRange(uint32_t startTick,
                                                 uint32_t endTick,
                                                 std::vector<const SeqTimedEvent*>& out) {
  out.clear();
  if (m_index == nullptr || !m_index->finalized()) {
    return;
  }

  if (endTick < startTick) {
    std::swap(startTick, endTick);
  }

  seek(startTick);

  out.reserve(m_active.size());
  for (Index idx : m_active) {
    out.push_back(&m_index->event(idx));
  }

  const auto& byStart = m_index->m_byStart;
  size_t scanPos = m_nextStart;
  while (scanPos < byStart.size()) {
    Index idx = byStart[scanPos];
    const auto& evt = m_index->event(idx);
    if (evt.startTick > endTick) {
      break;
    }
    out.push_back(&evt);
    scanPos++;
  }
}

void SeqEventTimeIndex::Cursor::rebuildActiveIndex() {
  m_active.clear();
  if (m_index == nullptr) {
    m_activePositions.clear();
    return;
  }
  m_activePositions.assign(m_index->size(), -1);
}

void SeqEventTimeIndex::Cursor::advanceTo(uint32_t tick) {
  if (m_index == nullptr) {
    return;
  }
  if (tick == m_currentTick) {
    return;
  }

  const auto& events = m_index->m_events;
  const auto& byStart = m_index->m_byStart;
  const auto& byEnd = m_index->m_byEnd;

  while (m_nextStart < byStart.size()) {
    Index idx = byStart[m_nextStart];
    if (events[idx].startTick > tick) {
      break;
    }
    addActive(idx);
    m_nextStart++;
  }

  while (m_nextEnd < byEnd.size()) {
    Index idx = byEnd[m_nextEnd];
    if (events[idx].endTickExclusive() > tick) {
      break;
    }
    removeActive(idx);
    m_nextEnd++;
  }

  m_currentTick = tick;
}

void SeqEventTimeIndex::Cursor::addActive(Index idx) {
  if (idx >= m_activePositions.size() || m_activePositions[idx] >= 0) {
    return;
  }
  m_activePositions[idx] = static_cast<int32_t>(m_active.size());
  m_active.push_back(idx);
}

void SeqEventTimeIndex::Cursor::removeActive(Index idx) {
  if (idx >= m_activePositions.size()) {
    return;
  }
  int32_t pos = m_activePositions[idx];
  if (pos < 0) {
    return;
  }
  Index lastIdx = m_active.back();
  m_active[static_cast<size_t>(pos)] = lastIdx;
  m_activePositions[lastIdx] = pos;
  m_active.pop_back();
  m_activePositions[idx] = -1;
}
