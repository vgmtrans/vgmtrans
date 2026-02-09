/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractScrollArea>

#include <bitset>
#include <vector>

#include "ActiveNoteFrameData.h"

class QResizeEvent;
class ActiveNoteRhiWidget;

class ActiveNoteView final : public QAbstractScrollArea {
public:
  struct ActiveKey {
    int trackIndex = -1;
    int key = -1;
  };

  explicit ActiveNoteView(QWidget* parent = nullptr);
  ~ActiveNoteView() override = default;

  void setTrackCount(int trackCount);
  void setActiveNotes(const std::vector<ActiveKey>& activeKeys, bool playbackActive);
  void clearPlaybackState();

  ActiveNoteFrame::Data captureRhiFrameData(float dpr) const;

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  static constexpr int kPianoKeyCount = 128;
  static constexpr int kRowHeight = 20;
  static constexpr int kTopPadding = 8;
  static constexpr int kBottomPadding = 8;
  static constexpr int kLeftGutter = 24;
  static constexpr int kRightPadding = 8;

  [[nodiscard]] QColor colorForTrack(int trackIndex) const;
  [[nodiscard]] int contentHeight() const;
  void updateScrollBars();
  void requestRender();

  ActiveNoteRhiWidget* m_rhiWidget = nullptr;
  int m_trackCount = 0;
  bool m_playbackActive = false;
  std::vector<QColor> m_trackColors;
  std::vector<std::bitset<kPianoKeyCount>> m_activeKeysByTrack;
};
