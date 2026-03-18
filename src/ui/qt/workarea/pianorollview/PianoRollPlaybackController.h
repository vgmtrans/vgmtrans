/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PianoRollFrameData.h"

#include <QtGlobal>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class PianoRollPlaybackController {
public:
  static constexpr int kMidiKeyCount = PianoRollFrame::kMidiKeyCount;

  struct ActiveKeyState {
    int trackIndex = -1;
    uint32_t startTick = 0;
  };

  struct TickUpdate {
    bool tickChanged = false;
    bool playbackStateChanged = false;
  };

  PianoRollPlaybackController();

  void clear();
  [[nodiscard]] TickUpdate setPlaybackTick(int tick,
                                           bool playbackActive,
                                           qint64 nowNs,
                                           double tempoBpm,
                                           int ppqn);
  void setCurrentTick(int tick);
  [[nodiscard]] bool applyResolvedActiveNotes(
      const std::vector<PianoRollFrame::Note>& resolvedActiveNotes,
      const std::function<bool(int trackIndex)>& isTrackEnabled);
  [[nodiscard]] float visualPlaybackTick(qint64 nowNs, int totalTicks, bool playerRunning) const;

  [[nodiscard]] bool playbackActive() const { return m_playbackActive; }
  [[nodiscard]] int currentTick() const { return m_currentTick; }
  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::Note>>& activeNotes() const {
    return m_activeNotes;
  }
  [[nodiscard]] const std::array<ActiveKeyState, kMidiKeyCount>& activeKeys() const {
    return m_activeKeys;
  }

private:
  bool m_playbackActive = false;
  int m_currentTick = 0;
  qint64 m_lastPlaybackTickUpdateNs = 0;
  float m_visualPlaybackTicksPerSecond = 0.0f;
  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_activeNotes;
  std::array<ActiveKeyState, kMidiKeyCount> m_activeKeys{};
};
