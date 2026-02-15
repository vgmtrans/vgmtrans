/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QPoint>
#include <QtGlobal>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "PianoRollFrameData.h"
#include "SeqEventTimeIndex.h"

class QEvent;
class QPointF;
class QRect;
class QRectF;
class QMouseEvent;
class QNativeGestureEvent;
class QResizeEvent;
class QShowEvent;
class QVariantAnimation;
class QWheelEvent;
class MidiTrack;
class SeqTrack;
class VGMSeq;
class VGMItem;
class PianoRollRhiHost;
class PianoRollRhiWindow;

class PianoRollView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit PianoRollView(QWidget* parent = nullptr);
  ~PianoRollView() override = default;

  void setSequence(VGMSeq* seq);
  void setTrackCount(int trackCount);
  void refreshSequenceData(bool allowTimelineBuild);
  void setSelectedItems(const std::vector<const VGMItem*>& items,
                        const VGMItem* primaryItem = nullptr);

  void setPlaybackTick(int tick, bool playbackActive);
  void clearPlaybackState();

  PianoRollFrame::Data captureRhiFrameData(float dpr) const;

signals:
  void seekRequested(int tick);
  void selectionChanged(VGMItem* item);
  void selectionSetChanged(const std::vector<VGMItem*>& items, VGMItem* primaryItem);
  void notePreviewRequested(const std::vector<VGMItem*>& items, VGMItem* anchorItem);
  void notePreviewStopped();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent* event) override;

private:
  friend class PianoRollRhiWidget;
  friend class PianoRollRhiWindow;

  struct ActiveKeyState {
    int trackIndex = -1;
    uint32_t startTick = 0;
  };

  struct SelectableNote {
    uint32_t startTick = 0;
    uint32_t duration = 0;
    uint8_t key = 0;
    int16_t trackIndex = -1;
    VGMItem* item = nullptr;
  };

  static constexpr int kMidiKeyCount = PianoRollFrame::kMidiKeyCount;
  static constexpr int kKeyboardWidth = 78;
  static constexpr int kTopBarHeight = 22;
  static constexpr size_t kInvalidNoteIndex = std::numeric_limits<size_t>::max();

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
  bool handleViewportCoalescedZoomGesture(float rawDelta,
                                          const QPointF& globalPos,
                                          Qt::KeyboardModifiers modifiers);
  bool handleViewportMousePress(QMouseEvent* event);
  bool handleViewportMouseMove(QMouseEvent* event);
  bool handleViewportMouseRelease(QMouseEvent* event);

  void maybeBuildTimelineFromSequence();
  void rebuildTrackIndexMap();
  // Resolves section-local track pointers back to logical track indices.
  int trackIndexForTrack(const SeqTrack* track) const;
  // Resolves a visual track index for timed events, including no-track/channel-only sequences.
  int trackIndexForEvent(const class SeqEvent* event) const;
  void rebuildTrackColors();
  void rebuildSequenceCache();
  bool updateActiveKeyStates();

  void updateScrollBars();
  void scheduleViewportSync();
  void requestRender();
  void requestRenderCoalesced();
  void scheduleCoalescedRender(int delayMs);

  int clampTick(int tick) const;
  int tickFromViewportX(int x) const;
  int scanlinePixelX(int tick) const;
  [[nodiscard]] int noteIndexAtViewportPoint(const QPoint& pos) const;
  [[nodiscard]] VGMItem* noteAtViewportPoint(const QPoint& pos) const;
  [[nodiscard]] QRect graphRectInViewport() const;
  [[nodiscard]] QRectF graphSelectionRectInViewport() const;
  void applySelectedNoteIndices(std::vector<size_t> indices,
                                bool emitSelectionSignal,
                                VGMItem* preferredPrimary = nullptr);
  void emitCurrentSelectionSignals();
  void updateMarqueeSelection(bool emitSelectionSignal);
  void updateMarqueePreview(const QPoint& cursorPos);
  void applyPreviewNoteIndices(std::vector<size_t> indices, size_t anchorIndex);
  void clearPreviewNotes();

  void zoomHorizontal(int steps, int anchorX, bool animated = false, int durationMs = 0);
  void zoomVertical(int steps, int anchorY, bool animated = false, int durationMs = 0);
  void zoomHorizontalFactor(float factor, int anchorX, bool animated, int durationMs);
  void zoomVerticalFactor(float factor, int anchorY, bool animated, int durationMs);

  void applyHorizontalScale(float scale, int anchorInNotes, float worldTickAtAnchor);
  void applyVerticalScale(float scale, int anchorInNotes, float worldYAtAnchor);
  void animateHorizontalScale(float targetScale, int anchorX, int durationMs);
  void animateVerticalScale(float targetScale, int anchorY, int durationMs);

  PianoRollRhiHost* m_rhiHost = nullptr;

  VGMSeq* m_seq = nullptr;
  const SeqEventTimeIndex* m_timeline = nullptr;
  std::unique_ptr<SeqEventTimeIndex::Cursor> m_timelineCursor;

  int m_trackCount = 0;
  int m_ppqn = 48;
  int m_totalTicks = 1;
  uint32_t m_maxNoteDurationTicks = 1;

  float m_pixelsPerTick = kDefaultPixelsPerTick;
  float m_pixelsPerKey = kDefaultPixelsPerKey;

  bool m_playbackActive = false;
  int m_currentTick = 0;
  bool m_seekDragActive = false;
  bool m_noteSelectionPressActive = false;
  bool m_noteSelectionDragging = false;
  bool m_attemptedTimelineBuild = false;
  // Initial centering waits for a real viewport size (stacked views can report tiny pre-layout sizes).
  bool m_initialViewportPositioned = false;
  bool m_viewportSyncQueued = false;
  bool m_coalescedRenderPending = false;

  QElapsedTimer m_animClock;
  QElapsedTimer m_renderClock;
  QVariantAnimation* m_horizontalZoomAnimation = nullptr;
  QVariantAnimation* m_verticalZoomAnimation = nullptr;
  float m_horizontalZoomWorldTick = 0.0f;
  int m_horizontalZoomAnchor = 0;
  float m_verticalZoomWorldY = 0.0f;
  int m_verticalZoomAnchor = 0;
  qint64 m_lastRenderMs = std::numeric_limits<qint64>::min() / 4;
  int m_lastRenderedScanlineX = std::numeric_limits<int>::min();

  size_t m_cachedTimelineSize = 0;
  bool m_cachedTimelineFinalized = false;

  QPoint m_noteSelectionAnchor;
  QPoint m_noteSelectionCurrent;
  VGMItem* m_primarySelectedItem = nullptr;
  size_t m_previewAnchorNoteIndex = kInvalidNoteIndex;

  // Primary map for regular sequences.
  std::unordered_map<const SeqTrack*, int> m_trackIndexByPtr;
  // Fallback map for multi-section sequences that swap SeqTrack objects.
  std::unordered_map<const MidiTrack*, int> m_trackIndexByMidiPtr;
  std::vector<QColor> m_trackColors;

  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_notes;
  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_activeNotes;
  std::shared_ptr<const std::vector<PianoRollFrame::Note>> m_selectedNotes;
  std::shared_ptr<const std::vector<PianoRollFrame::TimeSignature>> m_timeSignatures;
  std::vector<SelectableNote> m_selectableNotes;
  std::vector<size_t> m_selectedNoteIndices;
  std::vector<size_t> m_previewNoteIndices;

  std::array<ActiveKeyState, kMidiKeyCount> m_activeKeys{};
};
