/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PianoRollFrameData.h"
#include "SeqEventTimeIndex.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class MidiTrack;
class SeqEvent;
class SeqTrack;
class VGMItem;
class VGMSeq;

// Builds and owns the derived sequence snapshot consumed by PianoRollView and the renderer.
class PianoRollSequenceCache {
public:
  struct SelectableNote {
    uint32_t startTick = 0;
    uint32_t duration = 0;
    uint8_t key = 0;
    int16_t trackIndex = -1;
    VGMItem* item = nullptr;
    const SeqTimedEvent* timedEvent = nullptr;
  };

  PianoRollSequenceCache();

  void clear();
  void rebuild(VGMSeq* seq);
  [[nodiscard]] bool matchesTimelineState(const SeqEventTimeIndex& timeline) const;

  [[nodiscard]] int trackIndexForTrack(const SeqTrack* track) const;
  [[nodiscard]] int trackIndexForEvent(const SeqEvent* event) const;

  [[nodiscard]] const SeqEventTimeIndex* timeline() const { return m_timeline; }
  [[nodiscard]] const SeqEventTimeIndex::Cursor* timelineCursor() const { return m_timelineCursor.get(); }
  [[nodiscard]] SeqEventTimeIndex::Cursor* timelineCursor() { return m_timelineCursor.get(); }

  [[nodiscard]] int ppqn() const { return m_ppqn; }
  [[nodiscard]] int totalTicks() const { return m_totalTicks; }
  [[nodiscard]] uint32_t maxNoteDurationTicks() const { return m_maxNoteDurationTicks; }

  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::Note>>& notes() const { return m_notes; }
  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::TimeSignature>>& timeSignatures() const {
    return m_timeSignatures;
  }
  [[nodiscard]] const std::vector<SelectableNote>& selectableNotes() const { return m_selectableNotes; }
  [[nodiscard]] const std::unordered_map<const SeqTimedEvent*, uint8_t>& transposedKeyByTimedEvent() const {
    return m_transposedKeyByTimedEvent;
  }
  [[nodiscard]] const std::unordered_map<const SeqTimedEvent*, size_t>& noteIndexByTimedEvent() const {
    return m_noteIndexByTimedEvent;
  }

private:
  // Builds both direct track lookups and MidiTrack fallbacks for multi-section sequences.
  void rebuildTrackIndexMaps(VGMSeq* seq);

  const SeqEventTimeIndex* m_timeline = nullptr;
  std::unique_ptr<SeqEventTimeIndex::Cursor> m_timelineCursor;
  int m_ppqn = 48;
  int m_totalTicks = 1;
  uint32_t m_maxNoteDurationTicks = 1;
  size_t m_cachedTimelineSize = 0;
  bool m_cachedTimelineFinalized = false;

  std::unordered_map<const SeqTrack*, int> m_trackIndexByPtr;
  std::unordered_map<const MidiTrack*, int> m_trackIndexByMidiPtr;

  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_notes;
  std::shared_ptr<const std::vector<PianoRollFrame::TimeSignature>> m_timeSignatures;
  std::vector<SelectableNote> m_selectableNotes;
  std::unordered_map<const SeqTimedEvent*, uint8_t> m_transposedKeyByTimedEvent;
  std::unordered_map<const SeqTimedEvent*, size_t> m_noteIndexByTimedEvent;
};
