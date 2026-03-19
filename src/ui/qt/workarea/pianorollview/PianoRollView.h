/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QtGlobal>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "PianoRollFrameData.h"
#include "PianoRollGeometry.h"
#include "PianoRollPlaybackController.h"
#include "PianoRollSequenceCache.h"
#include "PianoRollSelectionModel.h"

class QEvent;
class QRect;
class QRectF;
class QKeyEvent;
class QMouseEvent;
class QNativeGestureEvent;
class QResizeEvent;
class QShowEvent;
class QTimerEvent;
class QVariantAnimation;
class QWheelEvent;
class VGMSeq;
class VGMItem;
class PianoRollRhiHost;
class PianoRollRhiWindow;
class RhiScrollAreaChrome;

class PianoRollView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  using PreviewSelection = PianoRollSelectionModel::PreviewSelection;

  explicit PianoRollView(QWidget* parent = nullptr);
  ~PianoRollView() override;

  void setSequence(VGMSeq* seq);
  void setTrackCount(int trackCount);
  void setTrackEnabledMask(std::vector<uint8_t> trackEnabledMask);
  void refreshSequenceData(bool allowTimelineBuild);
  void setSelectedItems(const std::vector<const VGMItem*>& items,
                        const VGMItem* primaryItem = nullptr,
                        bool revealSelection = true);

  void setPlaybackTick(int tick,
                       bool playbackActive,
                       const std::vector<PianoRollFrame::Note>* activeNotes = nullptr);
  void setPlaybackTickFromSeek(int tick,
                               bool playbackActive,
                               const std::vector<PianoRollFrame::Note>* activeNotes = nullptr);
  void clearPlaybackState();
  void setSmoothAutoScrollEnabled(bool enabled);
  [[nodiscard]] bool smoothAutoScrollEnabled() const;
  void ensureTickVisible(int tick, float viewportFraction = 0.10f, bool animated = false);

  PianoRollFrame::Data captureRhiFrameData(float dpr) const;

signals:
  void seekRequested(int tick);
  void selectionChanged(VGMItem* item);
  void selectionSetChanged(const std::vector<VGMItem*>& items, VGMItem* primaryItem);
  void notePreviewRequested(const std::vector<PreviewSelection>& notes, int tick);
  void notePreviewStopped();

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void timerEvent(QTimerEvent* event) override;

private:
  friend class PianoRollRhiWidget;
  friend class PianoRollRhiWindow;

  using SelectableNote = PianoRollSequenceCache::SelectableNote;

  struct FrameColors {
    QColor backgroundColor;
    QColor noteBackgroundColor;
    QColor keyboardBackgroundColor;
    QColor topBarBackgroundColor;
    QColor topBarProgressColor;
    QColor measureLineColor;
    QColor beatLineColor;
    QColor keySeparatorColor;
    QColor noteOutlineColor;
    QColor scanLineColor;
    QColor whiteKeyColor;
    QColor blackKeyColor;
    QColor whiteKeyRowColor;
    QColor blackKeyRowColor;
    QColor dividerColor;
    QColor selectedNoteFillColor;
    QColor selectedNoteOutlineColor;
    QColor selectionRectFillColor;
    QColor selectionRectOutlineColor;
  };

  static constexpr int kMidiKeyCount = PianoRollFrame::kMidiKeyCount;
  static constexpr int kKeyboardWidth = 78;
  static constexpr int kTopBarHeight = 22;
  static constexpr size_t kInvalidNoteIndex = std::numeric_limits<size_t>::max();

  static constexpr float kDefaultPixelsPerTick = 0.60f;
  static constexpr float kDefaultPixelsPerKey = 10.0f;
  static constexpr float kMinPixelsPerTick = 0.05f;
  static constexpr float kMaxPixelsPerTick = 6.0f;
  static constexpr float kMinPixelsPerKey = 5.0f;
  static constexpr float kMaxPixelsPerKey = 44.0f;

  [[nodiscard]] QColor colorForTrack(int trackIndex) const;

  bool handleViewportWheel(QWheelEvent* event);
  bool shouldAcceptViewportWheelScroll(Qt::ScrollPhase phase);
  bool handleViewportNativeGesture(QNativeGestureEvent* event);
  bool handleViewportCoalescedZoomGesture(float rawDelta,
                                          const QPointF& globalPos,
                                          Qt::KeyboardModifiers modifiers);
  bool handleViewportMousePress(QMouseEvent* event);
  bool handleViewportMouseMove(QMouseEvent* event);
  bool handleViewportMouseRelease(QMouseEvent* event);
  void handleViewportLeave();

  void maybeBuildTimelineFromSequence();
  [[nodiscard]] bool isTrackEnabled(int trackIndex) const;
  [[nodiscard]] int ppqn() const { return m_sequenceCache.ppqn(); }
  [[nodiscard]] int totalTicks() const { return m_sequenceCache.totalTicks(); }
  [[nodiscard]] uint32_t maxNoteDurationTicks() const { return m_sequenceCache.maxNoteDurationTicks(); }
  [[nodiscard]] bool playbackActive() const { return m_playbackController.playbackActive(); }
  [[nodiscard]] int currentTick() const { return m_playbackController.currentTick(); }
  [[nodiscard]] const SeqEventTimeIndex* timeline() const { return m_sequenceCache.timeline(); }
  [[nodiscard]] SeqEventTimeIndex::Cursor* timelineCursor() { return m_sequenceCache.timelineCursor(); }
  [[nodiscard]] const SeqEventTimeIndex::Cursor* timelineCursor() const { return m_sequenceCache.timelineCursor(); }
  [[nodiscard]] int trackIndexForEvent(const class SeqEvent* event) const {
    return m_sequenceCache.trackIndexForEvent(event);
  }
  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::Note>>& notes() const {
    return m_sequenceCache.notes();
  }
  [[nodiscard]] const std::shared_ptr<const std::vector<PianoRollFrame::TimeSignature>>& timeSignatures() const {
    return m_sequenceCache.timeSignatures();
  }
  [[nodiscard]] const std::vector<SelectableNote>& selectableNotes() const {
    return m_sequenceCache.selectableNotes();
  }
  [[nodiscard]] const std::unordered_map<const SeqTimedEvent*, uint8_t>& transposedKeyByTimedEvent() const {
    return m_sequenceCache.transposedKeyByTimedEvent();
  }
  [[nodiscard]] const std::unordered_map<const SeqTimedEvent*, size_t>& noteIndexByTimedEvent() const {
    return m_sequenceCache.noteIndexByTimedEvent();
  }
  [[nodiscard]] const std::vector<size_t>& selectedNoteIndices() const {
    return m_selectionModel.selectedNoteIndices();
  }
  [[nodiscard]] VGMItem* primarySelectedItem() const {
    return m_selectionModel.primarySelectedItem();
  }
  void resizeTrackEnabledMaskToTrackCount();
  void rebuildTrackColors();
  void rebuildFrameColors();
  void rebuildSequenceCache();
  void updateInactiveNoteDimTarget(bool animated = true);
  bool applyResolvedActiveNotes(const std::vector<PianoRollFrame::Note>& resolvedActiveNotes);
  bool updateActiveKeyStates();

  void ensureScrollChromeButtonsInstalled();
  void syncViewportLayoutState();
  void updateScrollBars();
  void requestRender();
  void requestRenderCoalesced();
  void scheduleCoalescedRender(int delayMs);
  void stopPlaybackAutoScrollAnimation();
  void drainPendingPlaybackAutoScroll();
  void advanceFrameDrivenPlaybackAutoScroll();
  [[nodiscard]] bool frameDrivenPlaybackAutoScrollActive() const;
  [[nodiscard]] bool shouldPumpPlaybackFrames() const;
  [[nodiscard]] float visualPlaybackTick() const;
  [[nodiscard]] PianoRollGeometry geometry() const;

  [[nodiscard]] QPoint viewportPosFromGlobal(const QPointF& globalPos) const;
  [[nodiscard]] QPointF autoScrollDeltaForGraphDrag(const QPoint& viewportPos) const;
  void stopDragAutoScroll();
  QPoint consumeDragAutoScrollDelta(const QPointF& dragDelta);
  void updateSeekDrag(int viewportX, bool forceEmit = false);
  void scrollTickToViewportFraction(int tick, float viewportFraction, bool animated = true);
  [[nodiscard]] int noteIndexAtViewportPoint(const QPoint& pos) const;
  [[nodiscard]] VGMItem* noteAtViewportPoint(const QPoint& pos) const;
  void updateSeekPreview();
  void previewSingleNoteAtViewportPoint(const QPoint& pos);
  void maybePausePlaybackForSeekDrag();
  void finishSeekDrag(bool commitPosition, int viewportX = 0);
  void applySelectedNoteIndices(std::vector<size_t> indices,
                                bool emitSelectionSignal,
                                VGMItem* preferredPrimary = nullptr);
  void emitCurrentSelectionSignals();
  void updateMarqueeSelection(bool emitSelectionSignal);
  void updateMarqueePreview(const QPoint& cursorPos);
  void applyPreviewNoteIndices(std::vector<size_t> indices, int previewTick);
  void clearPreviewNotes();
  void beginSeekDragAtX(int viewportX);

  void zoomHorizontalFromButton(int steps);
  void zoomHorizontal(int steps, int anchorX, bool animated = false, int durationMs = 0);
  void zoomVertical(int steps, int anchorY, bool animated = false, int durationMs = 0);
  void zoomHorizontalFactor(float factor, int anchorX, bool animated, int durationMs);
  void zoomVerticalFactor(float factor, int anchorY, bool animated, int durationMs);

  void applyHorizontalScale(float scale, int anchorInNotes, float worldTickAtAnchor);
  void applyVerticalScale(float scale, int anchorInNotes, float worldYAtAnchor);
  void animateHorizontalScale(float targetScale, int anchorX, int durationMs);
  void animateVerticalScale(float targetScale, int anchorY, int durationMs);
  [[nodiscard]] Qt::KeyboardModifiers mergedModifiers(Qt::KeyboardModifiers modifiers = Qt::NoModifier) const;
  void setPanDragActive(bool active, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
  void applyPanDragDelta(const QPoint& dragDelta);
  void setInteractionCursor(Qt::CursorShape shape);
  void refreshInteractionCursor(Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  PianoRollRhiHost* m_rhiHost = nullptr;
  std::unique_ptr<RhiScrollAreaChrome> m_scrollChrome;
  PianoRollPlaybackController m_playbackController;

  VGMSeq* m_seq = nullptr;
  PianoRollSequenceCache m_sequenceCache;

  int m_trackCount = 0;

  float m_pixelsPerTick = kDefaultPixelsPerTick;
  float m_pixelsPerKey = kDefaultPixelsPerKey;

  bool m_seekDragActive = false;
  bool m_lightFrameColors = true;
  bool m_panDragActive = false;
  bool m_noteSelectionPressActive = false;
  bool m_noteSelectionDragging = false;
  bool m_attemptedTimelineBuild = false;
  bool m_smoothAutoScrollEnabled = true;
  bool m_playbackAutoScrollEnabled = true;
  bool m_applyingPlaybackAutoScroll = false;
  bool m_applyingHorizontalZoomScroll = false;
  bool m_waitForWheelScrollBegin = false;
  // We wait for the initial paint event to fire to stop centering scrollbar on resize
  bool m_initialPaintEvent = false;
  bool m_coalescedRenderPending = false;
  bool m_frameDrivenPlaybackAutoScrollActive = false;
  bool m_scrollChromeButtonsInstalled = false;

  QElapsedTimer m_animClock;
  QElapsedTimer m_renderClock;
  QVariantAnimation* m_horizontalZoomAnimation = nullptr;
  QVariantAnimation* m_verticalZoomAnimation = nullptr;
  QVariantAnimation* m_playbackAutoScrollAnimation = nullptr;
  QVariantAnimation* m_inactiveNoteDimAnimation = nullptr;
  float m_inactiveNoteDimAlpha = 0.0f;
  float m_horizontalZoomWorldTick = 0.0f;
  int m_horizontalZoomAnchor = 0;
  float m_verticalZoomWorldY = 0.0f;
  int m_verticalZoomAnchor = 0;
  qint64 m_lastRenderMs = std::numeric_limits<qint64>::min() / 4;
  qint64 m_frameDrivenPlaybackAutoScrollStartNs = 0;
  int m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  int m_frameDrivenPlaybackAutoScrollStartX = 0;
  int m_frameDrivenPlaybackAutoScrollEndX = 0;

  QPoint m_noteSelectionAnchor;
  QPoint m_noteSelectionCurrent;
  QPoint m_noteSelectionAnchorWorld;
  QPointF m_dragAutoScrollRemainder;
  QPoint m_panDragLastPos;
  bool m_noteSelectionAnchorWorldValid = false;
  QBasicTimer m_dragAutoScrollTimer;
  bool m_resumePlaybackAfterSeekDrag = false;
  PianoRollSelectionModel m_selectionModel;

  std::vector<uint8_t> m_trackEnabledMask;
  std::vector<QColor> m_trackColors;
  std::shared_ptr<const std::vector<uint8_t>> m_trackEnabledMaskSnapshot;
  std::shared_ptr<const std::vector<QColor>> m_trackColorsSnapshot;
  FrameColors m_frameColors;
};
