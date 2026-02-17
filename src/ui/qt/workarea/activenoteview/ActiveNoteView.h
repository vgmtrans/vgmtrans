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
class QMouseEvent;
class QObject;
class QEvent;
class ActiveNoteRhiHost;
class ActiveNoteRhiWidget;
class ActiveNoteRhiWindow;

class ActiveNoteView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  struct ActiveKey {
    int trackIndex = -1;
    int key = -1;
  };

  explicit ActiveNoteView(QWidget* parent = nullptr);
  ~ActiveNoteView() override;

  void setTrackCount(int trackCount);
  void setActiveNotes(const std::vector<ActiveKey>& activeKeys, bool playbackActive);
  void clearPlaybackState();

  ActiveNoteFrame::Data captureRhiFrameData(float dpr) const;

signals:
  void notePreviewRequested(int trackIndex, int key);
  void notePreviewStopped();

protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  friend class ActiveNoteRhiWidget;
  friend class ActiveNoteRhiWindow;

  struct KeyHitResult {
    int trackIndex = -1;
    int key = -1;

    [[nodiscard]] bool valid() const { return trackIndex >= 0 && key >= 0; }
  };

  static constexpr int kPianoKeyCount = 128;
  static bool isBlackKey(int key);

  bool handleViewportMousePress(QMouseEvent* event);
  bool handleViewportMouseMove(QMouseEvent* event);
  bool handleViewportMouseRelease(QMouseEvent* event);
  void beginPreviewDrag();
  void endPreviewDrag();

  KeyHitResult hitTestKeyboardKey(const QPoint& pos) const;
  void setPreviewKey(const KeyHitResult& hit);
  void clearPreviewKey();

  [[nodiscard]] QColor colorForTrack(int trackIndex) const;
  void requestRender();

  ActiveNoteRhiHost* m_rhiHost = nullptr;
  int m_trackCount = 0;
  bool m_playbackActive = false;
  std::vector<QColor> m_trackColors;
  std::vector<std::bitset<kPianoKeyCount>> m_activeKeysByTrack;
  std::vector<std::bitset<kPianoKeyCount>> m_previewKeysByTrack;
  bool m_previewDragActive = false;
  bool m_previewGlobalTracking = false;
  int m_previewTrackIndex = -1;
  int m_previewKey = -1;
};
