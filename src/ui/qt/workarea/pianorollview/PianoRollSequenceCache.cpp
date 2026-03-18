/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollSequenceCache.h"

#include "SeqEvent.h"
#include "SeqNoteUtils.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <algorithm>
#include <limits>

namespace {

bool pianoRollNoteLess(const PianoRollFrame::Note& lhs, const PianoRollFrame::Note& rhs) {
  if (lhs.startTick != rhs.startTick) {
    return lhs.startTick < rhs.startTick;
  }
  if (lhs.key != rhs.key) {
    return lhs.key < rhs.key;
  }
  if (lhs.trackIndex != rhs.trackIndex) {
    return lhs.trackIndex < rhs.trackIndex;
  }
  return lhs.duration < rhs.duration;
}

}  // namespace

PianoRollSequenceCache::PianoRollSequenceCache() {
  clear();
}

void PianoRollSequenceCache::clear() {
  m_timeline = nullptr;
  m_timelineCursor.reset();
  m_ppqn = 48;
  m_totalTicks = 1;
  m_maxNoteDurationTicks = 1;
  m_cachedTimelineSize = 0;
  m_cachedTimelineFinalized = false;
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();
  m_selectableNotes.clear();
  m_transposedKeyByTimedEvent.clear();
  m_noteIndexByTimedEvent.clear();
  m_notes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_timeSignatures = std::make_shared<std::vector<PianoRollFrame::TimeSignature>>();
}

bool PianoRollSequenceCache::matchesTimelineState(const SeqEventTimeIndex& timeline) const {
  return m_timeline == &timeline &&
         m_cachedTimelineFinalized == timeline.finalized() &&
         m_cachedTimelineSize == timeline.size();
}

void PianoRollSequenceCache::rebuildTrackIndexMaps(VGMSeq* seq) {
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();

  if (!seq) {
    return;
  }

  for (size_t i = 0; i < seq->aTracks.size(); ++i) {
    if (auto* track = seq->aTracks[i]) {
      const int trackIndex = static_cast<int>(i);
      m_trackIndexByPtr[track] = trackIndex;
      if (track->pMidiTrack) {
        m_trackIndexByMidiPtr.emplace(track->pMidiTrack, trackIndex);
      }
    }
  }
}

int PianoRollSequenceCache::trackIndexForTrack(const SeqTrack* track) const {
  if (!track) {
    return -1;
  }

  const auto trackIt = m_trackIndexByPtr.find(track);
  if (trackIt != m_trackIndexByPtr.end()) {
    return trackIt->second;
  }

  if (track->pMidiTrack) {
    const auto midiTrackIt = m_trackIndexByMidiPtr.find(track->pMidiTrack);
    if (midiTrackIt != m_trackIndexByMidiPtr.end()) {
      return midiTrackIt->second;
    }
  }

  return -1;
}

int PianoRollSequenceCache::trackIndexForEvent(const SeqEvent* event) const {
  if (!event) {
    return -1;
  }

  const int trackIndex = trackIndexForTrack(event->parentTrack);
  if (trackIndex >= 0) {
    return trackIndex;
  }

  return static_cast<int>(event->channel);
}

void PianoRollSequenceCache::rebuild(VGMSeq* seq) {
  clear();

  if (!seq) {
    return;
  }

  m_ppqn = (seq->ppqn() > 0) ? seq->ppqn() : 48;
  rebuildTrackIndexMaps(seq);

  const auto& timeline = seq->timedEventIndex();
  m_timeline = &timeline;
  m_cachedTimelineFinalized = timeline.finalized();
  m_cachedTimelineSize = timeline.size();

  auto notes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  auto signatures = std::make_shared<std::vector<PianoRollFrame::TimeSignature>>();

  if (!timeline.finalized()) {
    signatures->push_back({0, 4, 4});
    m_notes = notes;
    m_timeSignatures = signatures;
    return;
  }

  m_timelineCursor = std::make_unique<SeqEventTimeIndex::Cursor>(timeline);

  uint64_t maxEndTick = 1;
  uint32_t maxDurationTicks = 1;
  notes->reserve(timeline.size());
  m_selectableNotes.reserve(timeline.size());
  for (size_t i = 0; i < timeline.size(); ++i) {
    const auto& timed = timeline.event(i);
    maxEndTick = std::max<uint64_t>(maxEndTick, timed.endTickExclusive());

    if (!timed.event) {
      continue;
    }

    const int noteKey = SeqNoteUtils::transposedNoteKeyForTimedEvent(seq, &timed);
    if (noteKey < 0 || noteKey >= PianoRollFrame::kMidiKeyCount) {
      continue;
    }
    m_transposedKeyByTimedEvent.emplace(&timed, static_cast<uint8_t>(noteKey));

    const int trackIndex = trackIndexForEvent(timed.event);
    if (trackIndex < 0) {
      continue;
    }

    const uint32_t noteDuration = std::max<uint32_t>(1, timed.duration);
    PianoRollFrame::Note note;
    note.startTick = timed.startTick;
    note.duration = noteDuration;
    note.key = static_cast<uint8_t>(noteKey);
    note.trackIndex = static_cast<int16_t>(trackIndex);
    notes->push_back(note);
    m_selectableNotes.push_back({
        timed.startTick,
        noteDuration,
        static_cast<uint8_t>(noteKey),
        static_cast<int16_t>(trackIndex),
        timed.event,
        &timed,
    });
    maxDurationTicks = std::max<uint32_t>(maxDurationTicks, note.duration);
  }

  std::sort(notes->begin(), notes->end(), pianoRollNoteLess);
  std::sort(m_selectableNotes.begin(), m_selectableNotes.end(), [](const auto& a, const auto& b) {
    if (a.startTick != b.startTick) {
      return a.startTick < b.startTick;
    }
    if (a.key != b.key) {
      return a.key < b.key;
    }
    if (a.trackIndex != b.trackIndex) {
      return a.trackIndex < b.trackIndex;
    }
    return a.duration < b.duration;
  });

  m_noteIndexByTimedEvent.reserve(m_selectableNotes.size());
  for (size_t i = 0; i < m_selectableNotes.size(); ++i) {
    if (m_selectableNotes[i].timedEvent) {
      m_noteIndexByTimedEvent.emplace(m_selectableNotes[i].timedEvent, i);
    }
  }

  if (!seq->aTracks.empty() && seq->aTracks.front()) {
    for (VGMItem* child : seq->aTracks.front()->children()) {
      auto* sig = dynamic_cast<TimeSigSeqEvent*>(child);
      if (!sig) {
        continue;
      }

      uint32_t tick = 0;
      if (!timeline.firstStartTick(sig, tick)) {
        continue;
      }

      signatures->push_back({
          tick,
          std::max<uint8_t>(1, sig->numer),
          std::max<uint8_t>(1, sig->denom),
      });
    }
  }

  if (signatures->empty()) {
    signatures->push_back({0, 4, 4});
  } else {
    std::sort(signatures->begin(), signatures->end(), [](const auto& a, const auto& b) {
      if (a.tick != b.tick) {
        return a.tick < b.tick;
      }
      if (a.numerator != b.numerator) {
        return a.numerator < b.numerator;
      }
      return a.denominator < b.denominator;
    });

    std::vector<PianoRollFrame::TimeSignature> deduped;
    deduped.reserve(signatures->size());
    for (const auto& sig : *signatures) {
      if (!deduped.empty() && deduped.back().tick == sig.tick) {
        deduped.back() = sig;
      } else {
        deduped.push_back(sig);
      }
    }

    signatures->assign(deduped.begin(), deduped.end());
    if (!signatures->empty() && signatures->front().tick != 0) {
      signatures->insert(signatures->begin(), {0, signatures->front().numerator, signatures->front().denominator});
    }
  }

  maxEndTick = std::min<uint64_t>(maxEndTick, static_cast<uint64_t>(std::numeric_limits<int>::max()));
  m_totalTicks = std::max(1, static_cast<int>(maxEndTick));
  m_maxNoteDurationTicks = std::max<uint32_t>(1, maxDurationTicks);
  m_notes = notes;
  m_timeSignatures = signatures;
}
