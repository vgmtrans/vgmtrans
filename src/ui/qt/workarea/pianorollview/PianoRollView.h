/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractScrollArea>
#include <QElapsedTimer>

#include <array>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "PianoRollFrameData.h"
#include "SeqEventTimeIndex.h"

class QEvent;
class QMouseEvent;
class QNativeGestureEvent;
class QResizeEvent;
class QShowEvent;
class QVariantAnimation;
class QWheelEvent;
class SeqTrack;
class VGMSeq;
class PianoRollRhiWidget;

class PianoRollView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit PianoRollView(QWidget* parent = nullptr);
  ~PianoRollView() override = default;

  void setSequence(VGMSeq* seq);
  void setTrackCount(int trackCount);
  void refreshSequenceData(bool allowTimelineBuild);

  void setPlaybackTick(int tick, bool playbackActive);
  void clearPlaybackState();

  PianoRollFrame::Data captureRhiFrameData(float dpr) const;

signals:
  void seekRequested(int tick);

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent* event) override;

private:
  friend class PianoRollRhiWidget;

  struct ActiveKeyState {
    int trackIndex = -1;
    uint32_t startTick = 0;
  };

  static constexpr int kMidiKeyCount = PianoRollFrame::kMidiKeyCount;
  static constexpr int kKeyboardWidth = 78;
  static constexpr int kTopBarHeight = 22;

  static constexpr float kDefaultPixelsPerTick = 0.20f;
  static constexpr float kDefaultPixelsPerKey = 12.0f;
  static constexpr float kMinPixelsPerTick = 0.0125f;
  static constexpr float kMaxPixelsPerTick = 6.0f;
  static constexpr float kMinPixelsPerKey = 3.0f;
  static constexpr float kMaxPixelsPerKey = 44.0f;

  static int noteKeyForEvent(const class SeqEvent* event);

  [[nodiscard]] QColor colorForTrack(int trackIndex) const;

  bool handleViewportWheel(QWheelEvent* event);
  bool handleViewportNativeGesture(QNativeGestureEvent* event);
  bool handleViewportMousePress(QMouseEvent* event);
  bool handleViewportMouseMove(QMouseEvent* event);
  bool handleViewportMouseRelease(QMouseEvent* event);

  void maybeBuildTimelineFromSequence();
  void rebuildTrackIndexMap();
  void rebuildTrackColors();
  void rebuildSequenceCache();
  bool updateActiveKeyStates();

  void updateScrollBars();
  void scheduleViewportSync();
  void requestRender();

  int clampTick(int tick) const;
  int tickFromViewportX(int x) const;
  int scanlinePixelX(int tick) const;

  void zoomHorizontal(int steps, int anchorX, bool animated = false, int durationMs = 0);
  void zoomVertical(int steps, int anchorY, bool animated = false, int durationMs = 0);
  void zoomHorizontalFactor(float factor, int anchorX, bool animated, int durationMs);
  void zoomVerticalFactor(float factor, int anchorY, bool animated, int durationMs);

  void applyHorizontalScale(float scale, int anchorInNotes, float worldTickAtAnchor);
  void applyVerticalScale(float scale, int anchorInNotes, float worldYAtAnchor);
  void animateHorizontalScale(float targetScale, int anchorX, int durationMs);
  void animateVerticalScale(float targetScale, int anchorY, int durationMs);

  PianoRollRhiWidget* m_rhiWidget = nullptr;

  VGMSeq* m_seq = nullptr;
  const SeqEventTimeIndex* m_timeline = nullptr;
  std::unique_ptr<SeqEventTimeIndex::Cursor> m_timelineCursor;

  int m_trackCount = 0;
  int m_ppqn = 48;
  int m_totalTicks = 1;

  float m_pixelsPerTick = kDefaultPixelsPerTick;
  float m_pixelsPerKey = kDefaultPixelsPerKey;

  bool m_playbackActive = false;
  int m_currentTick = 0;
  bool m_seekDragActive = false;
  bool m_attemptedTimelineBuild = false;
  bool m_initialViewportPositioned = false;
  bool m_viewportSyncQueued = false;

  QElapsedTimer m_animClock;
  QVariantAnimation* m_horizontalZoomAnimation = nullptr;
  QVariantAnimation* m_verticalZoomAnimation = nullptr;
  float m_horizontalZoomWorldTick = 0.0f;
  int m_horizontalZoomAnchor = 0;
  float m_verticalZoomWorldY = 0.0f;
  int m_verticalZoomAnchor = 0;
  int m_lastRenderedScanlineX = std::numeric_limits<int>::min();

  size_t m_cachedTimelineSize = 0;
  bool m_cachedTimelineFinalized = false;

  std::unordered_map<const SeqTrack*, int> m_trackIndexByPtr;
  std::vector<QColor> m_trackColors;

  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_notes;
  std::shared_ptr<const std::vector<PianoRollFrame::TimeSignature>> m_timeSignatures;

  std::array<ActiveKeyState, kMidiKeyCount> m_activeKeys{};
};
