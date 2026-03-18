/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollPlaybackController.h"

#include <algorithm>

namespace {

// Orders notes by visual position and returns true when lhs should be drawn before rhs.
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

// Returns true when two renderer-facing note records are identical.
bool pianoRollNoteEqual(const PianoRollFrame::Note& lhs, const PianoRollFrame::Note& rhs) {
  return lhs.startTick == rhs.startTick &&
         lhs.duration == rhs.duration &&
         lhs.key == rhs.key &&
         lhs.trackIndex == rhs.trackIndex;
}

}  // namespace

PianoRollPlaybackController::PianoRollPlaybackController() {
  clear();
}

void PianoRollPlaybackController::clear() {
  m_playbackActive = false;
  m_currentTick = 0;
  m_lastPlaybackTickUpdateNs = 0;
  m_visualPlaybackTicksPerSecond = 0.0f;
  m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  for (auto& state : m_activeKeys) {
    state.trackIndex = -1;
    state.startTick = 0;
  }
}

// Updates playback tick state and caches enough timing data to predict in-between frame positions.
PianoRollPlaybackController::TickUpdate PianoRollPlaybackController::setPlaybackTick(int tick,
                                                                                     bool playbackActive,
                                                                                     qint64 nowNs,
                                                                                     double tempoBpm,
                                                                                     int ppqn) {
  const TickUpdate update{
      .tickChanged = (tick != m_currentTick),
      .playbackStateChanged = (playbackActive != m_playbackActive),
  };

  m_currentTick = tick;
  m_playbackActive = playbackActive;
  if (m_playbackActive) {
    m_lastPlaybackTickUpdateNs = nowNs;
    m_visualPlaybackTicksPerSecond = static_cast<float>(
        (std::max(1.0, tempoBpm) * static_cast<double>(std::max(1, ppqn))) / 60.0);
  }

  return update;
}

void PianoRollPlaybackController::setCurrentTick(int tick) {
  m_currentTick = tick;
}

// Rebuilds active-note and active-key snapshots from the resolved playback notes.
bool PianoRollPlaybackController::applyResolvedActiveNotes(
    const std::vector<PianoRollFrame::Note>& resolvedActiveNotes,
    const std::function<bool(int trackIndex)>& isTrackEnabled) {
  std::array<ActiveKeyState, kMidiKeyCount> nextActiveKeys{};
  for (auto& state : nextActiveKeys) {
    state.trackIndex = -1;
    state.startTick = 0;
  }

  std::vector<PianoRollFrame::Note> nextActiveNotes;
  nextActiveNotes.reserve(resolvedActiveNotes.size());

  for (const auto& resolved : resolvedActiveNotes) {
    if (resolved.key >= kMidiKeyCount) {
      continue;
    }
    if (resolved.trackIndex < 0 || (isTrackEnabled && !isTrackEnabled(resolved.trackIndex))) {
      continue;
    }

    PianoRollFrame::Note note = resolved;
    note.duration = std::max<uint32_t>(1, note.duration);
    nextActiveNotes.push_back(note);

    // The newest note on a key owns the keyboard highlight for that key.
    auto& state = nextActiveKeys[static_cast<size_t>(note.key)];
    if (state.trackIndex < 0 || note.startTick >= state.startTick) {
      state.trackIndex = note.trackIndex;
      state.startTick = note.startTick;
    }
  }

  std::sort(nextActiveNotes.begin(), nextActiveNotes.end(), pianoRollNoteLess);

  bool activeKeysChanged = false;
  for (int key = 0; key < kMidiKeyCount; ++key) {
    const auto& oldState = m_activeKeys[static_cast<size_t>(key)];
    const auto& newState = nextActiveKeys[static_cast<size_t>(key)];
    if (oldState.trackIndex != newState.trackIndex || oldState.startTick != newState.startTick) {
      activeKeysChanged = true;
      break;
    }
  }

  bool activeNotesChanged = true;
  if (m_activeNotes && m_activeNotes->size() == nextActiveNotes.size()) {
    activeNotesChanged = false;
    for (size_t i = 0; i < nextActiveNotes.size(); ++i) {
      const auto& oldNote = (*m_activeNotes)[i];
      const auto& newNote = nextActiveNotes[i];
      if (!pianoRollNoteEqual(oldNote, newNote)) {
        activeNotesChanged = true;
        break;
      }
    }
  }
  if (activeNotesChanged || !m_activeNotes) {
    m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>(std::move(nextActiveNotes));
  }

  m_activeKeys = std::move(nextActiveKeys);
  return activeKeysChanged || activeNotesChanged;
}

// Predicts the current scanline tick between playback callbacks.
float PianoRollPlaybackController::visualPlaybackTick(qint64 nowNs, int totalTicks, bool playerRunning) const {
  const float baseTick = static_cast<float>(std::clamp(m_currentTick, 0, std::max(1, totalTicks)));
  if (!m_playbackActive || m_lastPlaybackTickUpdateNs <= 0 || m_visualPlaybackTicksPerSecond <= 0.0f) {
    return baseTick;
  }

  if (!playerRunning) {
    return baseTick;
  }

  const qint64 elapsedNs = std::max<qint64>(0, nowNs - m_lastPlaybackTickUpdateNs);
  const double predictedTick =
      static_cast<double>(baseTick) +
      (static_cast<double>(elapsedNs) * 1.0e-9 * static_cast<double>(m_visualPlaybackTicksPerSecond));
  return std::clamp(static_cast<float>(predictedTick),
                    0.0f,
                    static_cast<float>(std::max(1, totalTicks)));
}
