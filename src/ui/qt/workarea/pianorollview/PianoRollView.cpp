/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollView.h"

#include "common/DragAutoScroll.h"
#include "PianoRollRhiHost.h"
#include "SequencePlayer.h"
#include "workarea/rhi/RhiScrollAreaChrome.h"

#include "SeqEvent.h"
#include "SeqEventTimeIndex.h"
#include "SeqNoteUtils.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <QAbstractAnimation>
#include <QCursor>
#include <QEvent>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPalette>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QTimerEvent>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
constexpr int kNoteSelectionAutoScrollIntervalMs = 16;
constexpr int kPlaybackAutoScrollDurationMs = 1000;
constexpr int kInactiveNoteDimDurationMs = 160;
constexpr float kPlaybackPageTriggerFraction = 0.85f;
constexpr float kPlaybackPageTargetFraction = 0.05f;
constexpr float kInactiveNoteDimMaxAlpha = 0.30f;
const QColor kDisabledTrackColor(132, 132, 132);

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

bool pianoRollNoteEqual(const PianoRollFrame::Note& lhs, const PianoRollFrame::Note& rhs) {
  return lhs.startTick == rhs.startTick &&
         lhs.duration == rhs.duration &&
         lhs.key == rhs.key &&
         lhs.trackIndex == rhs.trackIndex;
}
}

PianoRollView::PianoRollView(QWidget* parent)
    : QAbstractScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);

  // The Qt scrollbars remain as hidden range/value models. The visible chrome
  // is computed separately and rendered inside the RHI surface.
  m_scrollChrome = std::make_unique<RhiScrollAreaChrome>(
      this,
      [this](const QMargins& margins) {
        setViewportMargins(margins);
      },
      [this]() {
        requestRender();
      });
  m_scrollChrome->setHorizontalPolicy(Qt::ScrollBarAlwaysOn);
  m_scrollChrome->setVerticalPolicy(Qt::ScrollBarAlwaysOn);
  m_scrollChrome->setHorizontalButtons({
      {RhiScrollButtonGlyph::Minus, [this]() { zoomHorizontalFromButton(-1); }},
      {RhiScrollButtonGlyph::Plus, [this]() { zoomHorizontalFromButton(+1); }},
  });
  m_scrollChrome->setVerticalButtons({
      {RhiScrollButtonGlyph::Minus, [this]() { zoomVertical(-1, viewport()->height() / 2, true, 150); }},
      {RhiScrollButtonGlyph::Plus, [this]() { zoomVertical(+1, viewport()->height() / 2, true, 150); }},
  });

  m_horizontalZoomAnimation = new QVariantAnimation(this);
  m_horizontalZoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_horizontalZoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    applyHorizontalScale(value.toFloat(), m_horizontalZoomAnchor, m_horizontalZoomWorldTick);
  });

  m_verticalZoomAnimation = new QVariantAnimation(this);
  m_verticalZoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_verticalZoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    applyVerticalScale(value.toFloat(), m_verticalZoomAnchor, m_verticalZoomWorldY);
  });

  m_playbackAutoScrollAnimation = new QVariantAnimation(this);
  m_playbackAutoScrollAnimation->setDuration(kPlaybackAutoScrollDurationMs);
  m_playbackAutoScrollAnimation->setEasingCurve(QEasingCurve::OutQuint);
  connect(m_playbackAutoScrollAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    auto* hbar = horizontalScrollBar();
    if (!hbar) {
      return;
    }

    const int nextValue = std::clamp(static_cast<int>(std::lround(value.toDouble())),
                                     hbar->minimum(),
                                     hbar->maximum());
    if (m_rhiHost && m_rhiHost->syncPlaybackAutoScrollToRenderFrame()) {
      // Native-window playback scroll is advanced once per rendered frame instead.
      return;
    }

    // QRhiWidget path applies animation steps immediately to avoid render-cadence stutter on Linux.
    if (nextValue == hbar->value()) {
      return;
    }
    m_applyingPlaybackAutoScroll = true;
    hbar->setValue(nextValue);
    m_applyingPlaybackAutoScroll = false;
  });

  m_inactiveNoteDimAnimation = new QVariantAnimation(this);
  m_inactiveNoteDimAnimation->setDuration(kInactiveNoteDimDurationMs);
  m_inactiveNoteDimAnimation->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_inactiveNoteDimAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    m_inactiveNoteDimAlpha = std::clamp(value.toFloat(), 0.0f, kInactiveNoteDimMaxAlpha);
    requestRenderCoalesced();
  });

  m_rhiHost = new PianoRollRhiHost(this, this);
  // Host spans the full scroll area so custom chrome can be rendered inside the
  // same surface while the hidden viewport keeps QAbstractScrollArea geometry.
  m_rhiHost->setGeometry(rect());
  m_rhiHost->setFocusPolicy(Qt::NoFocus);
  m_rhiHost->setMouseTracking(true);

  m_notes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_timeSignatures = std::make_shared<std::vector<PianoRollFrame::TimeSignature>>();
  m_trackEnabledMaskSnapshot = std::make_shared<std::vector<uint8_t>>();
  m_trackColorsSnapshot = std::make_shared<std::vector<QColor>>();
  rebuildFrameColors();

  for (auto& state : m_activeKeys) {
    state.trackIndex = -1;
    state.startTick = 0;
  }

  m_animClock.start();
  m_renderClock.start();
  syncViewportLayoutState();
  // Force a known default cursor to avoid stale inherited cursor state from
  // embedded RHI surfaces (notably on Windows).
  refreshInteractionCursor(Qt::NoModifier);
}

PianoRollView::~PianoRollView() = default;

void PianoRollView::setSequence(VGMSeq* seq) {
  if (seq == m_seq) {
    return;
  }

  stopPlaybackAutoScrollAnimation();
  clearPreviewNotes();

  m_seq = seq;
  m_timeline = nullptr;
  m_timelineCursor.reset();
  m_attemptedTimelineBuild = false;
  m_noteSelectionPressActive = false;
  m_noteSelectionDragging = false;
  m_noteSelectionAnchorWorldValid = false;
  m_cachedTimelineSize = 0;
  m_cachedTimelineFinalized = false;
  m_primarySelectedItem = nullptr;
  m_selectedNoteIndices.clear();
  m_previewNoteIndices.clear();
  m_previewAnchorNoteIndex = kInvalidNoteIndex;
  m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_transposedKeyByTimedEvent.clear();

  if (!m_seq) {
    m_trackCount = 0;
  } else {
    m_trackCount = std::max(m_trackCount, static_cast<int>(m_seq->aTracks.size()));
  }
  rebuildTrackIndexMap();
  resizeTrackEnabledMaskToTrackCount();
  rebuildTrackColors();
  rebuildSequenceCache();
  updateActiveKeyStates();
  updateScrollBars();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

void PianoRollView::setTrackCount(int trackCount) {
  trackCount = std::max(0, trackCount);
  if (trackCount == m_trackCount && static_cast<int>(m_trackColors.size()) == trackCount) {
    return;
  }

  m_trackCount = trackCount;
  resizeTrackEnabledMaskToTrackCount();
  rebuildTrackColors();
  updateActiveKeyStates();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

// Applies the effective per-track enabled mask used by playback highlighting.
void PianoRollView::setTrackEnabledMask(std::vector<uint8_t> trackEnabledMask) {
  const size_t desiredTrackCount = static_cast<size_t>(std::max(0, m_trackCount));
  if (trackEnabledMask.empty()) {
    trackEnabledMask.assign(desiredTrackCount, static_cast<uint8_t>(1));
  } else if (trackEnabledMask.size() < desiredTrackCount) {
    trackEnabledMask.resize(desiredTrackCount, static_cast<uint8_t>(1));
  }

  if (trackEnabledMask == m_trackEnabledMask) {
    return;
  }

  m_trackEnabledMask = std::move(trackEnabledMask);
  m_trackEnabledMaskSnapshot = std::make_shared<std::vector<uint8_t>>(m_trackEnabledMask);
  rebuildTrackColors();
  updateActiveKeyStates();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

void PianoRollView::refreshSequenceData(bool allowTimelineBuild) {
  if (!m_seq) {
    return;
  }

  if (allowTimelineBuild) {
    maybeBuildTimelineFromSequence();
  }

  const int seqTrackCount = static_cast<int>(m_seq->aTracks.size());
  if (seqTrackCount > m_trackCount) {
    m_trackCount = seqTrackCount;
    rebuildTrackIndexMap();
    resizeTrackEnabledMaskToTrackCount();
    rebuildTrackColors();
  }

  const auto& timeline = m_seq->timedEventIndex();
  const bool timelineFinalized = timeline.finalized();
  const size_t timelineSize = timeline.size();

  if (m_timeline == &timeline &&
      m_cachedTimelineFinalized == timelineFinalized &&
      m_cachedTimelineSize == timelineSize) {
    // Timeline identity + size/finalized status unchanged -> cached notes still valid.
    return;
  }

  rebuildSequenceCache();
  updateActiveKeyStates();
  updateScrollBars();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

void PianoRollView::setSelectedItems(const std::vector<const VGMItem*>& items,
                                     const VGMItem* primaryItem) {
  if (m_selectableNotes.empty()) {
    applySelectedNoteIndices({}, false, nullptr);
    return;
  }

  std::unordered_set<const VGMItem*> selectedItemSet;
  selectedItemSet.reserve(items.size() * 2 + 1);
  for (const auto* item : items) {
    if (item) {
      selectedItemSet.insert(item);
    }
  }

  std::vector<size_t> indices;
  indices.reserve(m_selectableNotes.size());
  for (size_t i = 0; i < m_selectableNotes.size(); ++i) {
    if (selectedItemSet.find(m_selectableNotes[i].item) != selectedItemSet.end()) {
      indices.push_back(i);
    }
  }

  applySelectedNoteIndices(std::move(indices), false, const_cast<VGMItem*>(primaryItem));
}

void PianoRollView::setPlaybackTick(int tick,
                                    bool playbackActive,
                                    const std::vector<PianoRollFrame::Note>* activeNotes) {
  tick = clampTick(tick);
  const bool tickChanged = (tick != m_currentTick);
  const bool playbackStateChanged = (playbackActive != m_playbackActive);
  if (!tickChanged && !playbackStateChanged && !activeNotes) {
    return;
  }

  m_currentTick = tick;
  m_playbackActive = playbackActive;
  if (m_playbackActive) {
    m_lastPlaybackTickUpdateNs = m_animClock.nsecsElapsed();
    m_visualPlaybackTicksPerSecond = static_cast<float>(
        (std::max(1.0, SequencePlayer::the().currentTempoBpm()) *
         static_cast<double>(std::max(1, m_ppqn))) / 60.0);
  }
  updateInactiveNoteDimTarget();

  if (playbackStateChanged && !m_playbackActive) {
    stopPlaybackAutoScrollAnimation();
    m_waitForWheelScrollBegin = false;
  }

  if (playbackStateChanged && m_playbackActive) {
    m_playbackAutoScrollEnabled = true;
    m_waitForWheelScrollBegin = true;
    if (!isPlaybackTickVisible(tick)) {
      scrollPlaybackTickToViewportFraction(tick, kPlaybackPageTargetFraction, m_smoothAutoScrollEnabled);
    }
  }

  if (m_playbackActive && m_playbackAutoScrollEnabled) {
    const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
    if (noteViewportWidth > 0) {
      const int triggerX = static_cast<int>(std::lround(
          static_cast<float>(noteViewportWidth) * kPlaybackPageTriggerFraction));
      if (scanlinePixelX(tick) >= triggerX) {
        // Keep one smooth page-scroll animation alive and retarget it as needed.
        scrollPlaybackTickToViewportFraction(tick, kPlaybackPageTargetFraction, m_smoothAutoScrollEnabled);
      }
    }
  }

  const bool activeKeysChanged = activeNotes
      ? applyResolvedActiveNotes(*activeNotes)
      : updateActiveKeyStates();
  const int scanX = scanlinePixelX(tick);
  const bool scanlineChanged = (scanX != m_lastRenderedScanlineX);
  if (playbackStateChanged || activeKeysChanged || scanlineChanged) {
    m_lastRenderedScanlineX = scanX;
    requestRender();
  }
}

void PianoRollView::clearPlaybackState() {
  stopPlaybackAutoScrollAnimation();
  m_playbackActive = false;
  m_currentTick = 0;
  m_lastPlaybackTickUpdateNs = 0;
  m_visualPlaybackTicksPerSecond = 0.0f;
  m_playbackAutoScrollEnabled = true;
  m_waitForWheelScrollBegin = false;
  m_applyingPlaybackAutoScroll = false;
  updateInactiveNoteDimTarget();
  updateActiveKeyStates();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

void PianoRollView::setSmoothAutoScrollEnabled(bool enabled) {
  if (m_smoothAutoScrollEnabled == enabled) {
    return;
  }

  m_smoothAutoScrollEnabled = enabled;
  if (!m_smoothAutoScrollEnabled) {
    stopPlaybackAutoScrollAnimation();
  }
}

bool PianoRollView::smoothAutoScrollEnabled() const {
  return m_smoothAutoScrollEnabled;
}

PianoRollFrame::Data PianoRollView::captureRhiFrameData(float dpr) const {
  PianoRollFrame::Data frame;
  frame.viewportSize = size();
  frame.dpr = std::max(1.0f, dpr);

  frame.totalTicks = m_totalTicks;
  frame.currentTick = clampTick(m_currentTick);
  // Active-note resolution stays callback-driven, but the scanline uses a
  // predicted float tick so playback motion can stay smooth between callbacks.
  frame.visualCurrentTick = visualPlaybackTick();
  frame.trackCount = m_trackCount;
  frame.ppqn = std::max(1, m_ppqn);
  frame.maxNoteDurationTicks = std::max<uint32_t>(1, m_maxNoteDurationTicks);

  frame.scrollX = horizontalScrollBar()->value();
  frame.scrollY = verticalScrollBar()->value();
  frame.keyboardWidth = kKeyboardWidth;
  frame.topBarHeight = kTopBarHeight;
  frame.pixelsPerTick = m_pixelsPerTick;
  frame.minPixelsPerTick = kMinPixelsPerTick;
  frame.pixelsPerKey = m_pixelsPerKey;
  frame.elapsedSeconds = static_cast<float>(m_animClock.elapsed()) / 1000.0f;
  frame.inactiveNoteDimAlpha = m_inactiveNoteDimAlpha;
  frame.playbackActive = m_playbackActive;
  if (m_scrollChrome) {
    frame.scrollChrome = m_scrollChrome->snapshot();
  }

  frame.trackColors = m_trackColorsSnapshot;
  frame.trackEnabled = m_trackEnabledMaskSnapshot;
  frame.notes = m_notes;
  frame.activeNotes = m_activeNotes;
  frame.selectedNotes = m_selectedNotes;
  frame.timeSignatures = m_timeSignatures;

  frame.activeKeyTrack.fill(-1);
  for (int key = 0; key < kMidiKeyCount; ++key) {
    frame.activeKeyTrack[static_cast<size_t>(key)] = m_activeKeys[static_cast<size_t>(key)].trackIndex;
  }

  frame.backgroundColor = m_frameColors.backgroundColor;
  frame.noteBackgroundColor = m_frameColors.noteBackgroundColor;
  frame.keyboardBackgroundColor = m_frameColors.keyboardBackgroundColor;
  frame.topBarBackgroundColor = m_frameColors.topBarBackgroundColor;
  frame.topBarProgressColor = m_frameColors.topBarProgressColor;
  frame.measureLineColor = m_frameColors.measureLineColor;
  frame.beatLineColor = m_frameColors.beatLineColor;
  frame.keySeparatorColor = m_frameColors.keySeparatorColor;
  frame.noteOutlineColor = m_frameColors.noteOutlineColor;
  frame.scanLineColor = m_frameColors.scanLineColor;
  frame.whiteKeyColor = m_frameColors.whiteKeyColor;
  frame.blackKeyColor = m_frameColors.blackKeyColor;
  frame.whiteKeyRowColor = m_frameColors.whiteKeyRowColor;
  frame.blackKeyRowColor = m_frameColors.blackKeyRowColor;
  frame.dividerColor = m_frameColors.dividerColor;
  frame.selectedNoteFillColor = m_frameColors.selectedNoteFillColor;
  frame.selectedNoteOutlineColor = m_frameColors.selectedNoteOutlineColor;
  frame.selectionRectFillColor = m_frameColors.selectionRectFillColor;
  frame.selectionRectOutlineColor = m_frameColors.selectionRectOutlineColor;

  if (m_noteSelectionDragging) {
    const QRectF marquee = graphSelectionRectInViewport();
    if (!marquee.isEmpty()) {
      frame.selectionRectVisible = true;
      frame.selectionRectX = static_cast<float>(marquee.left());
      frame.selectionRectY = static_cast<float>(marquee.top());
      frame.selectionRectW = static_cast<float>(marquee.width());
      frame.selectionRectH = static_cast<float>(marquee.height());
    }
  }

  return frame;
}

void PianoRollView::paintEvent(QPaintEvent* event) {
  // Latch once the first paint pass starts so initial layout recentering stops.
  m_initialPaintEvent = true;
  QAbstractScrollArea::paintEvent(event);
}

void PianoRollView::syncViewportLayoutState() {
  if (m_scrollChrome) {
    // Recompute viewport margins before sizing the hidden scroll models.
    m_scrollChrome->syncLayout();
  }
  updateScrollBars();
  if (m_scrollChrome) {
    m_scrollChrome->syncLayout();
  }
  if (m_rhiHost) {
    m_rhiHost->setGeometry(rect());
  }
  requestRender();
}

void PianoRollView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  syncViewportLayoutState();
}

void PianoRollView::showEvent(QShowEvent* event) {
  QAbstractScrollArea::showEvent(event);
  syncViewportLayoutState();
}

void PianoRollView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  Q_UNUSED(dy);
  if (dx != 0 && m_playbackActive && !m_applyingPlaybackAutoScroll && !m_applyingHorizontalZoomScroll) {
    stopPlaybackAutoScrollAnimation();
    m_playbackAutoScrollEnabled = false;
  }
  requestRenderCoalesced();
}

void PianoRollView::changeEvent(QEvent* event) {
  QAbstractScrollArea::changeEvent(event);

  if (!event) {
    return;
  }

  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange ||
      event->type() == QEvent::StyleChange) {
    rebuildFrameColors();
    updateInactiveNoteDimTarget(false);
    if (m_scrollChrome) {
      m_scrollChrome->syncLayout();
    }
    requestRender();
  }
}

void PianoRollView::keyPressEvent(QKeyEvent* event) {
  QAbstractScrollArea::keyPressEvent(event);
  refreshInteractionCursor();
}

void PianoRollView::keyReleaseEvent(QKeyEvent* event) {
  QAbstractScrollArea::keyReleaseEvent(event);
  refreshInteractionCursor();
}

void PianoRollView::timerEvent(QTimerEvent* event) {
  if (!event || event->timerId() != m_noteSelectionAutoScrollTimer.timerId()) {
    QAbstractScrollArea::timerEvent(event);
    return;
  }

  const bool leftDown = (QGuiApplication::mouseButtons() & Qt::LeftButton);
  if (!leftDown || !m_noteSelectionPressActive || !m_noteSelectionDragging) {
    stopNoteSelectionAutoScroll();
    return;
  }

  // Keep edge auto-scroll running even when the pointer stops moving.
  const QPoint pos = viewportPosFromGlobal(QPointF(QCursor::pos()));
  const QPointF autoDelta = autoScrollDeltaForGraphDrag(pos);
  if (autoDelta.isNull()) {
    stopNoteSelectionAutoScroll();
    return;
  }

  const QPoint panDelta = consumeAutoScrollDelta(autoDelta);
  m_noteSelectionCurrent = pos;
  if (!panDelta.isNull()) {
    applyPanDragDelta(panDelta);
    updateMarqueeSelection(false);
    updateMarqueePreview(pos);
    requestRenderCoalesced();
  }
}

Qt::KeyboardModifiers PianoRollView::mergedModifiers(Qt::KeyboardModifiers modifiers) const {
  return modifiers | QGuiApplication::queryKeyboardModifiers();
}

void PianoRollView::setPanDragActive(bool active, Qt::KeyboardModifiers modifiers) {
  m_panDragActive = active;
  refreshInteractionCursor(modifiers);
}

void PianoRollView::applyPanDragDelta(const QPoint& dragDelta) {
  if (dragDelta.isNull()) {
    return;
  }

  auto* hbar = horizontalScrollBar();
  auto* vbar = verticalScrollBar();
  if (hbar) {
    hbar->setValue(std::clamp(hbar->value() - dragDelta.x(), hbar->minimum(), hbar->maximum()));
  }
  if (vbar) {
    vbar->setValue(std::clamp(vbar->value() - dragDelta.y(), vbar->minimum(), vbar->maximum()));
  }
  requestRenderCoalesced();
}

void PianoRollView::stopNoteSelectionAutoScroll() {
  m_noteSelectionAutoScrollTimer.stop();
  m_noteSelectionAutoScrollRemainder = {};
}

QPoint PianoRollView::consumeAutoScrollDelta(const QPointF& dragDelta) {
  // Preserve sub-pixel drag speed and emit whole-pixel scroll steps over time.
  m_noteSelectionAutoScrollRemainder += dragDelta;
  const int stepX = static_cast<int>(std::trunc(m_noteSelectionAutoScrollRemainder.x()));
  const int stepY = static_cast<int>(std::trunc(m_noteSelectionAutoScrollRemainder.y()));
  m_noteSelectionAutoScrollRemainder -= QPointF(stepX, stepY);
  return QPoint(stepX, stepY);
}

void PianoRollView::setInteractionCursor(Qt::CursorShape shape) {
  const QCursor cursor(shape);
  setCursor(cursor);
  viewport()->setCursor(cursor);
  if (m_rhiHost) {
    m_rhiHost->setSurfaceCursor(shape);
  }
}

void PianoRollView::refreshInteractionCursor(Qt::KeyboardModifiers modifiers) {
  if (m_panDragActive) {
    setInteractionCursor(Qt::ClosedHandCursor);
    return;
  }

  const Qt::KeyboardModifiers activeModifiers = mergedModifiers(modifiers);
  if (activeModifiers.testFlag(Qt::AltModifier)) {
    setInteractionCursor(Qt::OpenHandCursor);
  } else {
    setInteractionCursor(Qt::ArrowCursor);
  }
}

QColor PianoRollView::colorForTrack(int trackIndex) const {
  const int hue = (trackIndex * 43) % 360;
  return QColor::fromHsv(hue, 190, 235);
}

bool PianoRollView::handleViewportWheel(QWheelEvent* event) {
  if (!event) {
    return false;
  }

  constexpr float kWheelZoomSensitivity = 0.11f;
  constexpr float kWheelZoomMinFactor = 0.80f;
  constexpr float kWheelZoomMaxFactor = 1.30f;
  constexpr float kAngleDeltaUnit = 120.0f;
  constexpr float kPixelDeltaUnit = 24.0f;
  constexpr int kWheelZoomAnimationMs = 40;

  const Qt::KeyboardModifiers mods = event->modifiers();
  refreshInteractionCursor(mods);
  const bool zoomHorizontalOnly = mods.testFlag(Qt::ControlModifier);
  const bool zoomVerticalOnly = mods.testFlag(Qt::AltModifier);
  if (!zoomHorizontalOnly && !zoomVerticalOnly) {
    return false;
  }

  float wheelUnits = 0.0f;
  const QPoint pixelDelta = event->pixelDelta();
  if (pixelDelta.y() != 0) {
    wheelUnits = static_cast<float>(pixelDelta.y()) / kPixelDeltaUnit;
  } else if (pixelDelta.x() != 0) {
    wheelUnits = static_cast<float>(pixelDelta.x()) / kPixelDeltaUnit;
  } else {
    const QPoint angleDelta = event->angleDelta();
    if (angleDelta.y() != 0) {
      wheelUnits = static_cast<float>(angleDelta.y()) / kAngleDeltaUnit;
    } else if (angleDelta.x() != 0) {
      wheelUnits = static_cast<float>(angleDelta.x()) / kAngleDeltaUnit;
    }
  }

  if (std::abs(wheelUnits) < 0.0001f) {
    event->accept();
    return true;
  }

  const float factor =
      std::clamp(std::exp(wheelUnits * kWheelZoomSensitivity), kWheelZoomMinFactor, kWheelZoomMaxFactor);

  const QPoint pos = event->position().toPoint();
  if (zoomHorizontalOnly) {
    zoomHorizontalFactor(factor, pos.x(), true, kWheelZoomAnimationMs);
  }
  if (zoomVerticalOnly) {
    zoomVerticalFactor(factor, pos.y(), true, kWheelZoomAnimationMs);
  }

  event->accept();
  return true;
}

// Filters stale trackpad momentum after play/resume until a new wheel gesture begins.
bool PianoRollView::shouldAcceptViewportWheelScroll(Qt::ScrollPhase phase) {
  if (!m_waitForWheelScrollBegin) {
    return true;
  }

  if (phase == Qt::ScrollBegin || phase == Qt::NoScrollPhase) {
    m_waitForWheelScrollBegin = false;
    return true;
  }

  return false;
}

bool PianoRollView::handleViewportNativeGesture(QNativeGestureEvent* event) {
  if (!event || event->gestureType() != Qt::ZoomNativeGesture) {
    return false;
  }

  const float rawDelta = static_cast<float>(event->value());
  const bool handled =
      handleViewportCoalescedZoomGesture(rawDelta, event->globalPosition(), event->modifiers());
  event->accept();
  return handled;
}

bool PianoRollView::handleViewportCoalescedZoomGesture(float rawDelta,
                                                       const QPointF& globalPos,
                                                       Qt::KeyboardModifiers modifiers) {
  // Ignore tiny gesture noise to avoid constant micro-updates while fingers rest.
  if (std::abs(rawDelta) < 0.01f) {
    return true;
  }

  const float factor = std::clamp(std::exp(rawDelta), 0.55f, 1.85f);

  QPoint anchor = globalPos.toPoint();
  if (m_rhiHost) {
    // Gestures arrive in global space from the render surface.
    anchor = m_rhiHost->mapFromGlobal(anchor);
  } else {
    anchor = viewport()->mapFromGlobal(anchor);
  }

  // Native gesture modifiers can be stale/empty on some backends. Merge with
  // live keyboard state so Option/Alt pinch reliably drives Y-axis zoom.
  const Qt::KeyboardModifiers activeModifiers = mergedModifiers(modifiers);
  refreshInteractionCursor(activeModifiers);
  if (activeModifiers.testFlag(Qt::AltModifier)) {
    zoomVerticalFactor(factor, anchor.y(), false, 0);
  } else {
    zoomHorizontalFactor(factor, anchor.x(), false, 0);
  }

  return true;
}

bool PianoRollView::handleViewportMousePress(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    return false;
  }

  stopNoteSelectionAutoScroll();
  const Qt::KeyboardModifiers activeModifiers = mergedModifiers(event->modifiers());
  const QPoint pos = viewportPosFromGlobal(event->globalPosition());
  if (m_scrollChrome && m_scrollChrome->handleMousePress(pos)) {
    refreshInteractionCursor(activeModifiers);
    event->accept();
    return true;
  }
  if (activeModifiers.testFlag(Qt::AltModifier)) {
    clearPreviewNotes();
    m_seekDragActive = false;
    updateInactiveNoteDimTarget();
    m_noteSelectionPressActive = false;
    m_noteSelectionDragging = false;
    m_noteSelectionAnchorWorldValid = false;
    setPanDragActive(true, activeModifiers);
    m_panDragLastPos = pos;
    event->accept();
    return true;
  }

  if (activeModifiers.testFlag(Qt::ControlModifier)) {
    beginSeekDragAtX(pos.x());
    event->accept();
    return true;
  }

  if (pos.y() < kTopBarHeight && pos.x() >= kKeyboardWidth) {
    beginSeekDragAtX(pos.x());
    event->accept();
    return true;
  }

  if (pos.y() >= kTopBarHeight && pos.x() >= kKeyboardWidth) {
    m_noteSelectionPressActive = true;
    m_noteSelectionDragging = false;
    m_noteSelectionAnchor = pos;
    // Keep the marquee origin in graph space so it stays attached to content while panning.
    m_noteSelectionAnchorWorld = graphWorldPosFromViewport(pos);
    m_noteSelectionAnchorWorldValid = true;
    m_noteSelectionCurrent = pos;
    previewSingleNoteAtViewportPoint(pos);

    event->accept();
    return true;
  }

  return false;
}

void PianoRollView::beginSeekDragAtX(int viewportX) {
  clearPreviewNotes();
  m_noteSelectionPressActive = false;
  m_noteSelectionDragging = false;
  m_noteSelectionAnchorWorldValid = false;
  stopPlaybackAutoScrollAnimation();
  m_playbackAutoScrollEnabled = true;
  m_seekDragActive = true;
  updateInactiveNoteDimTarget();
  m_currentTick = tickFromViewportX(viewportX);
  updateActiveKeyStates();
  requestRender();
  emit seekRequested(m_currentTick);
}

bool PianoRollView::handleViewportMouseMove(QMouseEvent* event) {
  if (!event) {
    return false;
  }

  const Qt::KeyboardModifiers activeModifiers = mergedModifiers(event->modifiers());
  // Some RHI backends can report empty event->buttons() when dragging leaves
  // the render surface. Merge with global button state to keep drags active.
  const Qt::MouseButtons activeButtons = event->buttons() | QGuiApplication::mouseButtons();
  const QPoint pos = viewportPosFromGlobal(event->globalPosition());
  const bool contentInteractionActive = m_panDragActive || m_seekDragActive || m_noteSelectionPressActive;
  if (!contentInteractionActive && m_scrollChrome && m_scrollChrome->handleMouseMove(pos, activeButtons)) {
    refreshInteractionCursor(activeModifiers);
    event->accept();
    return true;
  }
  if (m_panDragActive) {
    if (!(activeButtons & Qt::LeftButton)) {
      setPanDragActive(false, activeModifiers);
      event->accept();
      return true;
    }

    const QPoint dragDelta = pos - m_panDragLastPos;
    m_panDragLastPos = pos;
    applyPanDragDelta(dragDelta);
    refreshInteractionCursor(activeModifiers);
    event->accept();
    return true;
  }

  if (m_seekDragActive) {
    if (!(activeButtons & Qt::LeftButton)) {
      m_seekDragActive = false;
      updateInactiveNoteDimTarget();
      event->accept();
      return true;
    }

    m_currentTick = tickFromViewportX(pos.x());
    updateActiveKeyStates();
    requestRender();
    emit seekRequested(m_currentTick);

    event->accept();
    return true;
  }

  if (!m_noteSelectionPressActive) {
    stopNoteSelectionAutoScroll();
    refreshInteractionCursor(activeModifiers);
    return false;
  }

  if (!(activeButtons & Qt::LeftButton)) {
    stopNoteSelectionAutoScroll();
    m_noteSelectionPressActive = false;
    m_noteSelectionDragging = false;
    m_noteSelectionAnchorWorldValid = false;
    clearPreviewNotes();
    requestRender();
    refreshInteractionCursor(activeModifiers);
    event->accept();
    return true;
  }

  m_noteSelectionCurrent = pos;
  const QPoint dragDelta = m_noteSelectionCurrent - m_noteSelectionAnchor;
  const bool movedEnough = std::abs(dragDelta.x()) >= 2 || std::abs(dragDelta.y()) >= 2;
  if (!m_noteSelectionDragging && movedEnough) {
    m_noteSelectionDragging = true;
  }

  if (m_noteSelectionDragging) {
    // Timer-driven only: mouse-move just arms/disarms edge auto-scroll.
    const QPointF autoDelta = autoScrollDeltaForGraphDrag(pos);
    if (autoDelta.isNull()) {
      stopNoteSelectionAutoScroll();
    } else {
      if (!m_noteSelectionAutoScrollTimer.isActive()) {
        m_noteSelectionAutoScrollTimer.start(kNoteSelectionAutoScrollIntervalMs, this);
      }
    }
    updateMarqueeSelection(false);
    updateMarqueePreview(pos);
    requestRenderCoalesced();
  } else {
    stopNoteSelectionAutoScroll();
    previewSingleNoteAtViewportPoint(pos);
  }

  refreshInteractionCursor(activeModifiers);
  event->accept();
  return true;
}

bool PianoRollView::handleViewportMouseRelease(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    return false;
  }

  stopNoteSelectionAutoScroll();
  const Qt::KeyboardModifiers activeModifiers = mergedModifiers(event->modifiers());
  const QPoint pos = viewportPosFromGlobal(event->globalPosition());
  if (!m_panDragActive && !m_seekDragActive && !m_noteSelectionPressActive &&
      m_scrollChrome && m_scrollChrome->handleMouseRelease(pos)) {
    refreshInteractionCursor(activeModifiers);
    event->accept();
    return true;
  }
  if (m_panDragActive) {
    setPanDragActive(false, activeModifiers);
    event->accept();
    return true;
  }

  if (m_seekDragActive) {
    m_currentTick = tickFromViewportX(pos.x());
    m_seekDragActive = false;
    updateInactiveNoteDimTarget();
    updateActiveKeyStates();
    requestRender();

    event->accept();
    return true;
  }

  if (!m_noteSelectionPressActive) {
    return false;
  }

  m_noteSelectionCurrent = pos;
  if (m_noteSelectionDragging) {
    updateMarqueeSelection(false);
    emitCurrentSelectionSignals();
  } else {
    const int noteIndex = noteIndexAtViewportPoint(pos);
    if (noteIndex >= 0) {
      applySelectedNoteIndices({static_cast<size_t>(noteIndex)},
                               true,
                               m_selectableNotes[static_cast<size_t>(noteIndex)].item);
    } else {
      applySelectedNoteIndices({}, true, nullptr);
    }
  }

  clearPreviewNotes();
  m_noteSelectionPressActive = false;
  const bool wasDragging = m_noteSelectionDragging;
  m_noteSelectionDragging = false;
  m_noteSelectionAnchorWorldValid = false;
  if (wasDragging) {
    requestRender();
  }

  event->accept();
  refreshInteractionCursor(activeModifiers);
  return true;
}

void PianoRollView::handleViewportLeave() {
  stopNoteSelectionAutoScroll();
  if (m_scrollChrome) {
    m_scrollChrome->handleLeave();
  }
  refreshInteractionCursor();
}

void PianoRollView::maybeBuildTimelineFromSequence() {
  if (!m_seq || m_attemptedTimelineBuild) {
    return;
  }

  auto& timeline = m_seq->timedEventIndex();
  if (timeline.finalized()) {
    return;
  }

  m_attemptedTimelineBuild = true;

  // Converting to MIDI finalizes timedEventIndex for views that need absolute note timing.
  const VGMColl* coll = m_seq->assocColls.empty() ? nullptr : m_seq->assocColls.front();
  MidiFile* midi = m_seq->convertToMidi(coll);
  delete midi;
}

void PianoRollView::rebuildTrackIndexMap() {
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();

  if (!m_seq) {
    return;
  }

  for (size_t i = 0; i < m_seq->aTracks.size(); ++i) {
    if (auto* track = m_seq->aTracks[i]) {
      const int trackIndex = static_cast<int>(i);
      m_trackIndexByPtr[track] = trackIndex;
      if (track->pMidiTrack) {
        m_trackIndexByMidiPtr.emplace(track->pMidiTrack, trackIndex);
      }
    }
  }
}

int PianoRollView::trackIndexForTrack(const SeqTrack* track) const {
  if (!track) {
    return -1;
  }

  const auto trackIt = m_trackIndexByPtr.find(track);
  if (trackIt != m_trackIndexByPtr.end()) {
    return trackIt->second;
  }

  // Multi-section sequences can emit events from section track instances no longer
  // present in aTracks. Resolve by shared MidiTrack when available.
  if (track->pMidiTrack) {
    const auto midiTrackIt = m_trackIndexByMidiPtr.find(track->pMidiTrack);
    if (midiTrackIt != m_trackIndexByMidiPtr.end()) {
      return midiTrackIt->second;
    }
  }

  return -1;
}

int PianoRollView::trackIndexForEvent(const SeqEvent* event) const {
  if (!event) {
    return -1;
  }

  const int trackIndex = trackIndexForTrack(event->parentTrack);
  if (trackIndex >= 0) {
    return trackIndex;
  }

  return static_cast<int>(event->channel);
}

bool PianoRollView::isTrackEnabled(int trackIndex) const {
  if (trackIndex < 0) {
    return true;
  }
  if (trackIndex >= static_cast<int>(m_trackEnabledMask.size())) {
    return true;
  }
  return m_trackEnabledMask[static_cast<size_t>(trackIndex)] != 0;
}

void PianoRollView::resizeTrackEnabledMaskToTrackCount() {
  const size_t desired = static_cast<size_t>(std::max(0, m_trackCount));
  if (m_trackEnabledMask.size() == desired) {
    return;
  }
  m_trackEnabledMask.resize(desired, static_cast<uint8_t>(1));
  m_trackEnabledMaskSnapshot = std::make_shared<std::vector<uint8_t>>(m_trackEnabledMask);
}

void PianoRollView::rebuildTrackColors() {
  m_trackColors.resize(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i) {
    m_trackColors[static_cast<size_t>(i)] = isTrackEnabled(i) ? colorForTrack(i) : kDisabledTrackColor;
  }
  m_trackColorsSnapshot = std::make_shared<std::vector<QColor>>(m_trackColors);
}

void PianoRollView::rebuildFrameColors() {
  const QPalette pal = palette();
  const QColor base = pal.color(QPalette::Base);
  const QColor window = pal.color(QPalette::Window);
  const QColor text = pal.color(QPalette::Text);
  const bool dark = base.lightnessF() < 0.5f;
  m_lightFrameColors = !dark;

  m_frameColors.backgroundColor = window;
  m_frameColors.noteBackgroundColor = dark ? base.lighter(108) : base;
  m_frameColors.keyboardBackgroundColor = dark ? QColor(31, 35, 40) : QColor(219, 223, 228);
  m_frameColors.topBarBackgroundColor = dark ? QColor(47, 52, 60) : QColor(210, 216, 224);
  m_frameColors.topBarProgressColor = dark ? QColor(108, 125, 158, 85) : QColor(100, 129, 180, 70);
  m_frameColors.measureLineColor = dark ? QColor(160, 168, 182, 82) : QColor(102, 111, 127, 54);
  m_frameColors.beatLineColor = dark ? QColor(156, 164, 178, 38) : QColor(110, 119, 136, 26);
  m_frameColors.keySeparatorColor = dark ? QColor(20, 24, 28, 140) : QColor(108, 116, 128, 70);
  m_frameColors.noteOutlineColor = dark ? QColor(18, 20, 22, 180) : QColor(62, 70, 84, 130);
  m_frameColors.scanLineColor = dark ? QColor(255, 94, 77, 230) : QColor(201, 56, 36, 220);
  m_frameColors.whiteKeyColor = dark ? QColor(188, 193, 202) : QColor(242, 244, 247);
  m_frameColors.blackKeyColor = dark ? QColor(58, 64, 74) : QColor(86, 93, 104);
  m_frameColors.whiteKeyRowColor = dark ? QColor(76, 84, 95, 42) : QColor(235, 233, 230);
  m_frameColors.blackKeyRowColor = dark ? QColor(44, 50, 59, 68) : QColor(223, 221, 218);
  m_frameColors.dividerColor = dark ? text.lighter(115) : text.darker(125);
  m_frameColors.dividerColor.setAlpha(dark ? 74 : 62);
  m_frameColors.selectedNoteFillColor = dark ? QColor(72, 182, 255, 72) : QColor(52, 148, 225, 66);
  m_frameColors.selectedNoteOutlineColor = dark ? QColor(138, 214, 255, 245) : QColor(36, 110, 196, 230);
  m_frameColors.selectionRectFillColor = dark ? QColor(78, 184, 255, 42) : QColor(72, 158, 232, 40);
  m_frameColors.selectionRectOutlineColor = dark ? QColor(138, 214, 255, 212) : QColor(36, 110, 196, 205);
}

void PianoRollView::updateInactiveNoteDimTarget(bool animated) {
  const float targetAlpha = (m_lightFrameColors && (m_playbackActive || m_seekDragActive))
                                ? kInactiveNoteDimMaxAlpha
                                : 0.0f;
  if (m_inactiveNoteDimAnimation &&
      m_inactiveNoteDimAnimation->state() == QAbstractAnimation::Running &&
      std::abs(targetAlpha - m_inactiveNoteDimAnimation->endValue().toFloat()) <= 0.001f) {
    return;
  }
  if (std::abs(targetAlpha - m_inactiveNoteDimAlpha) <= 0.001f) {
    return;
  }

  if (!m_inactiveNoteDimAnimation || !animated) {
    if (m_inactiveNoteDimAnimation) {
      m_inactiveNoteDimAnimation->stop();
    }
    m_inactiveNoteDimAlpha = targetAlpha;
    requestRender();
    return;
  }

  m_inactiveNoteDimAnimation->stop();
  m_inactiveNoteDimAnimation->setStartValue(m_inactiveNoteDimAlpha);
  m_inactiveNoteDimAnimation->setEndValue(targetAlpha);
  m_inactiveNoteDimAnimation->start();
}

void PianoRollView::rebuildSequenceCache() {
  m_ppqn = (m_seq && m_seq->ppqn() > 0) ? m_seq->ppqn() : 48;
  m_maxNoteDurationTicks = 1;
  m_selectableNotes.clear();
  m_transposedKeyByTimedEvent.clear();
  m_selectedNoteIndices.clear();
  m_primarySelectedItem = nullptr;
  m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();

  auto notes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  auto signatures = std::make_shared<std::vector<PianoRollFrame::TimeSignature>>();

  m_totalTicks = 1;
  m_timeline = nullptr;
  m_timelineCursor.reset();

  if (!m_seq) {
    m_notes = notes;
    m_timeSignatures = signatures;
    m_cachedTimelineFinalized = false;
    m_cachedTimelineSize = 0;
    return;
  }

  rebuildTrackIndexMap();

  const auto& timeline = m_seq->timedEventIndex();
  m_timeline = &timeline;
  m_cachedTimelineFinalized = timeline.finalized();
  m_cachedTimelineSize = timeline.size();

  if (!timeline.finalized()) {
    // Rendering proceeds with empty notes until the timeline is finalized.
    m_notes = notes;
    signatures->push_back({0, 4, 4});
    m_timeSignatures = signatures;
    m_selectableNotes.clear();
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

    int noteKey = SeqNoteUtils::transposedNoteKeyForTimedEvent(m_seq, &timed);
    if (noteKey < 0 || noteKey >= kMidiKeyCount) {
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
    });
    maxDurationTicks = std::max<uint32_t>(maxDurationTicks, note.duration);
  }

  std::sort(notes->begin(), notes->end(), [](const auto& a, const auto& b) {
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

  if (!m_seq->aTracks.empty() && m_seq->aTracks.front()) {
    for (VGMItem* child : m_seq->aTracks.front()->children()) {
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
  m_currentTick = clampTick(m_currentTick);
}

bool PianoRollView::updateActiveKeyStates() {
  std::vector<PianoRollFrame::Note> resolvedActiveNotes;

  if (m_playbackActive && m_timeline && m_timelineCursor && m_timeline->finalized()) {
    std::vector<const SeqTimedEvent*> active;
    m_timelineCursor->getActiveAt(static_cast<uint32_t>(std::max(0, m_currentTick)), active);
    resolvedActiveNotes.reserve(active.size());

    for (const auto* timed : active) {
      if (!timed || !timed->event) {
        continue;
      }

      int key = -1;
      const auto keyIt = m_transposedKeyByTimedEvent.find(timed);
      if (keyIt != m_transposedKeyByTimedEvent.end()) {
        key = keyIt->second;
      } else {
        key = SeqNoteUtils::transposedNoteKeyForTimedEvent(m_seq, timed);
      }
      if (key < 0 || key >= kMidiKeyCount) {
        continue;
      }

      const int trackIndex = trackIndexForEvent(timed->event);
      if (trackIndex < 0) {
        continue;
      }

      resolvedActiveNotes.push_back({
          timed->startTick,
          std::max<uint32_t>(1, timed->duration),
          static_cast<uint8_t>(key),
          static_cast<int16_t>(trackIndex),
      });
    }
  }

  return applyResolvedActiveNotes(resolvedActiveNotes);
}

bool PianoRollView::applyResolvedActiveNotes(const std::vector<PianoRollFrame::Note>& resolvedActiveNotes) {
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
    if (resolved.trackIndex < 0 || !isTrackEnabled(resolved.trackIndex)) {
      continue;
    }

    PianoRollFrame::Note note = resolved;
    note.duration = std::max<uint32_t>(1, note.duration);
    nextActiveNotes.push_back(note);

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

void PianoRollView::updateScrollBars() {
  const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
  const int noteViewportHeight = std::max(0, viewport()->height() - kTopBarHeight);

  const int contentWidth = std::max(
      1,
      static_cast<int>(std::ceil(static_cast<float>(m_totalTicks) * m_pixelsPerTick)));
  const int contentHeight = std::max(
      1,
      static_cast<int>(std::ceil(static_cast<float>(kMidiKeyCount) * m_pixelsPerKey)));

  horizontalScrollBar()->setRange(0, std::max(0, contentWidth - noteViewportWidth));
  horizontalScrollBar()->setPageStep(noteViewportWidth);
  horizontalScrollBar()->setSingleStep(std::max(1, noteViewportWidth / 20));

  verticalScrollBar()->setRange(0, std::max(0, contentHeight - noteViewportHeight));
  verticalScrollBar()->setPageStep(noteViewportHeight);
  verticalScrollBar()->setSingleStep(std::max(1, static_cast<int>(std::round(m_pixelsPerKey * 2.0f))));

  if (!m_initialPaintEvent) {
    verticalScrollBar()->setValue(verticalScrollBar()->maximum() / 2);
  }

  m_currentTick = clampTick(m_currentTick);
}

void PianoRollView::requestRender() {
  if (m_rhiHost) {
    m_rhiHost->requestUpdate();
    if (!m_renderClock.isValid()) {
      m_renderClock.start();
    }
    m_lastRenderMs = m_renderClock.elapsed();
  }
}

void PianoRollView::requestRenderCoalesced() {
  if (!m_rhiHost) {
    return;
  }

  if (!m_renderClock.isValid()) {
    m_renderClock.start();
  }

  static constexpr qint64 kMinRenderIntervalMs = 16;
  const qint64 nowMs = m_renderClock.elapsed();
  const qint64 sinceLastRender = nowMs - m_lastRenderMs;
  // Scroll gestures and hover updates can arrive much faster than the surface
  // can present. Coalesce them so we request at most one fresh frame per beat.
  const int delayMs = (sinceLastRender >= kMinRenderIntervalMs)
                          ? 0
                          : static_cast<int>(kMinRenderIntervalMs - sinceLastRender);
  scheduleCoalescedRender(delayMs);
}

void PianoRollView::scheduleCoalescedRender(int delayMs) {
  if (m_coalescedRenderPending) {
    return;
  }

  m_coalescedRenderPending = true;
  QTimer::singleShot(std::max(0, delayMs), this, [this]() {
    m_coalescedRenderPending = false;
    requestRender();
  });
}

void PianoRollView::stopPlaybackAutoScrollAnimation() {
  if (m_playbackAutoScrollAnimation && m_playbackAutoScrollAnimation->state() == QAbstractAnimation::Running) {
    m_playbackAutoScrollAnimation->stop();
  }
  m_frameDrivenPlaybackAutoScrollActive = false;
  m_applyingPlaybackAutoScroll = false;
}

void PianoRollView::drainPendingPlaybackAutoScroll() {
  advanceFrameDrivenPlaybackAutoScroll();
}

void PianoRollView::advanceFrameDrivenPlaybackAutoScroll() {
  if (!m_frameDrivenPlaybackAutoScrollActive) {
    return;
  }

  auto* hbar = horizontalScrollBar();
  if (!hbar) {
    m_frameDrivenPlaybackAutoScrollActive = false;
    return;
  }

  static const QEasingCurve kPlaybackAutoScrollEasing(QEasingCurve::OutQuint);
  const qint64 elapsedNs = std::max<qint64>(0, m_animClock.nsecsElapsed() - m_frameDrivenPlaybackAutoScrollStartNs);
  const float progress = std::clamp(
      static_cast<float>(elapsedNs) / (static_cast<float>(kPlaybackAutoScrollDurationMs) * 1000000.0f),
      0.0f,
      1.0f);
  const float eased = static_cast<float>(kPlaybackAutoScrollEasing.valueForProgress(progress));
  const int nextValue = std::clamp(
      static_cast<int>(std::lround(
          m_frameDrivenPlaybackAutoScrollStartX +
          (static_cast<float>(m_frameDrivenPlaybackAutoScrollEndX - m_frameDrivenPlaybackAutoScrollStartX) * eased))),
      hbar->minimum(),
      hbar->maximum());
  if (nextValue == hbar->value()) {
    if (progress >= 1.0f || nextValue == m_frameDrivenPlaybackAutoScrollEndX) {
      m_frameDrivenPlaybackAutoScrollActive = false;
    }
    return;
  }

  m_applyingPlaybackAutoScroll = true;
  hbar->setValue(nextValue);
  m_applyingPlaybackAutoScroll = false;
  if (progress >= 1.0f || nextValue == m_frameDrivenPlaybackAutoScrollEndX) {
    m_frameDrivenPlaybackAutoScrollActive = false;
  }
}

bool PianoRollView::frameDrivenPlaybackAutoScrollActive() const {
  return m_frameDrivenPlaybackAutoScrollActive;
}

bool PianoRollView::shouldPumpPlaybackFrames() const {
  return m_playbackActive || m_frameDrivenPlaybackAutoScrollActive;
}

float PianoRollView::visualPlaybackTick() const {
  const float baseTick = static_cast<float>(clampTick(m_currentTick));
  if (!m_playbackActive || m_lastPlaybackTickUpdateNs <= 0 || m_visualPlaybackTicksPerSecond <= 0.0f) {
    return baseTick;
  }

  if (!SequencePlayer::the().playing()) {
    return baseTick;
  }

  const qint64 nowNs = m_animClock.nsecsElapsed();
  const qint64 elapsedNs = std::max<qint64>(0, nowNs - m_lastPlaybackTickUpdateNs);
  const double predictedTick =
      static_cast<double>(baseTick) +
      (static_cast<double>(elapsedNs) * 1.0e-9 * static_cast<double>(m_visualPlaybackTicksPerSecond));
  return std::clamp(static_cast<float>(predictedTick),
                    0.0f,
                    static_cast<float>(std::max(1, m_totalTicks)));
}

QPoint PianoRollView::viewportPosFromGlobal(const QPointF& globalPos) const {
  const QPoint globalPoint = globalPos.toPoint();
  if (m_rhiHost) {
    return m_rhiHost->mapFromGlobal(globalPoint);
  }
  if (viewport()) {
    return viewport()->mapFromGlobal(globalPoint);
  }
  return globalPoint;
}

QPoint PianoRollView::graphWorldPosFromViewport(const QPoint& viewportPos) const {
  return QPoint(horizontalScrollBar()->value() + (viewportPos.x() - kKeyboardWidth),
                verticalScrollBar()->value() + (viewportPos.y() - kTopBarHeight));
}

QPoint PianoRollView::graphViewportPosFromWorld(const QPoint& worldPos) const {
  return QPoint(kKeyboardWidth + (worldPos.x() - horizontalScrollBar()->value()),
                kTopBarHeight + (worldPos.y() - verticalScrollBar()->value()));
}

QPointF PianoRollView::autoScrollDeltaForGraphDrag(const QPoint& viewportPos) const {
  const QRect graphRect = graphRectInViewport();
  if (graphRect.isEmpty()) {
    return {};
  }

  // Base dead-zone / ramp / max-step profile; helper adapts it to per-edge monitor travel.
  static constexpr QtUi::AutoScrollRampConfig kAutoScrollRampConfig{48, 40, 320};

  const QtUi::ScreenEdgeTravelPixels travel = QtUi::screenEdgeTravelPixels(viewport(), graphRect);
  QPointF delta = QtUi::edgeAutoScrollDelta(viewportPos, graphRect, travel, kAutoScrollRampConfig);
  const qreal currentDpr = std::max<qreal>(1.0, viewport() ? viewport()->devicePixelRatioF() : devicePixelRatioF());
  return delta / currentDpr;
}

int PianoRollView::clampTick(int tick) const {
  return std::clamp(tick, 0, std::max(1, m_totalTicks));
}

int PianoRollView::tickFromViewportX(int x) const {
  if (m_pixelsPerTick <= 0.0f) {
    return 0;
  }

  const int localX = std::max(0, x - kKeyboardWidth);
  const float absoluteX = static_cast<float>(horizontalScrollBar()->value() + localX);
  const int tick = static_cast<int>(std::llround(absoluteX / m_pixelsPerTick));
  return clampTick(tick);
}

int PianoRollView::scanlinePixelX(int tick) const {
  const float absoluteX = static_cast<float>(clampTick(tick)) * std::max(0.0001f, m_pixelsPerTick);
  const float viewX = absoluteX - static_cast<float>(horizontalScrollBar()->value());
  return static_cast<int>(std::lround(viewX));
}

// Returns whether the playback scanline is currently visible in the note viewport.
bool PianoRollView::isPlaybackTickVisible(int tick) const {
  const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
  if (noteViewportWidth <= 0) {
    return true;
  }

  const int scanX = scanlinePixelX(tick);
  return scanX >= 0 && scanX < noteViewportWidth;
}

// Repositions horizontal scroll so the playback scanline lands at the requested viewport fraction.
void PianoRollView::scrollPlaybackTickToViewportFraction(int tick, float viewportFraction, bool animated) {
  auto* hbar = horizontalScrollBar();
  if (!hbar) {
    return;
  }

  const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
  if (noteViewportWidth <= 0) {
    return;
  }

  const float clampedFraction = std::clamp(viewportFraction, 0.0f, 1.0f);
  const int scanlineWorldX = static_cast<int>(std::lround(
      static_cast<float>(clampTick(tick)) * std::max(0.0001f, m_pixelsPerTick)));
  const int desiredViewportX =
      static_cast<int>(std::lround(static_cast<float>(noteViewportWidth) * clampedFraction));
  const int desiredScrollX = scanlineWorldX - desiredViewportX;
  const int clampedScrollX = std::clamp(desiredScrollX, hbar->minimum(), hbar->maximum());
  if (clampedScrollX == hbar->value()) {
    return;
  }
  if (!animated || !m_playbackAutoScrollAnimation) {
    m_frameDrivenPlaybackAutoScrollActive = false;
    m_applyingPlaybackAutoScroll = true;
    hbar->setValue(clampedScrollX);
    m_applyingPlaybackAutoScroll = false;
    return;
  }

  if (m_rhiHost && m_rhiHost->syncPlaybackAutoScrollToRenderFrame()) {
    if (m_frameDrivenPlaybackAutoScrollActive && m_frameDrivenPlaybackAutoScrollEndX == clampedScrollX) {
      return;
    }

    if (m_playbackAutoScrollAnimation->state() == QAbstractAnimation::Running) {
      m_playbackAutoScrollAnimation->stop();
    }

    // Native-window auto-scroll advances from rendered frames so the playback
    // page turn stays smooth even when timer callbacks and frame delivery drift.
    m_frameDrivenPlaybackAutoScrollActive = true;
    m_frameDrivenPlaybackAutoScrollStartNs = m_animClock.nsecsElapsed();
    m_frameDrivenPlaybackAutoScrollStartX = hbar->value();
    m_frameDrivenPlaybackAutoScrollEndX = clampedScrollX;
    requestRender();
    return;
  }

  const bool animationRunning = (m_playbackAutoScrollAnimation->state() == QAbstractAnimation::Running);
  if (animationRunning) {
    const int currentEnd = std::clamp(static_cast<int>(std::lround(m_playbackAutoScrollAnimation->endValue().toDouble())),
                                      hbar->minimum(),
                                      hbar->maximum());
    if (currentEnd == clampedScrollX) {
      return;
    }

    // Under heavy CPU load, restarting each tick can starve progress. Retarget
    // the current animation instead so it keeps moving and naturally catches up.
    m_playbackAutoScrollAnimation->setEndValue(static_cast<qreal>(clampedScrollX));
    return;
  }

  m_frameDrivenPlaybackAutoScrollActive = false;
  m_playbackAutoScrollAnimation->setStartValue(static_cast<qreal>(hbar->value()));
  m_playbackAutoScrollAnimation->setEndValue(static_cast<qreal>(clampedScrollX));
  m_playbackAutoScrollAnimation->start();
}

QRect PianoRollView::graphRectInViewport() const {
  return QRect(kKeyboardWidth,
               kTopBarHeight,
               std::max(0, viewport()->width() - kKeyboardWidth),
               std::max(0, viewport()->height() - kTopBarHeight));
}

QRectF PianoRollView::graphSelectionRectInViewport() const {
  const QRect graphRect = graphRectInViewport();
  if (graphRect.isEmpty()) {
    return {};
  }

  // Convert world-space anchor back to viewport so marquee origin tracks scrolled content.
  const QPoint anchorViewport = m_noteSelectionAnchorWorldValid
                                    ? graphViewportPosFromWorld(m_noteSelectionAnchorWorld)
                                    : m_noteSelectionAnchor;
  const float anchorX = static_cast<float>(anchorViewport.x());
  const float anchorY = static_cast<float>(anchorViewport.y());
  const float currentX = static_cast<float>(m_noteSelectionCurrent.x());
  const float currentY = static_cast<float>(m_noteSelectionCurrent.y());

  const float left = std::min(anchorX, currentX);
  const float top = std::min(anchorY, currentY);
  const float right = std::max(anchorX, currentX);
  const float bottom = std::max(anchorY, currentY);
  return QRectF(left, top, right - left, bottom - top);
}

void PianoRollView::normalizeNoteIndices(std::vector<size_t>& indices) const {
  indices.erase(std::remove_if(indices.begin(),
                               indices.end(),
                               [this](size_t index) {
                                 return index >= m_selectableNotes.size();
                               }),
                indices.end());
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

bool PianoRollView::isSelectedItem(VGMItem* item) const {
  if (!item) {
    return false;
  }

  for (size_t selectedIndex : m_selectedNoteIndices) {
    if (m_selectableNotes[selectedIndex].item == item) {
      return true;
    }
  }
  return false;
}

std::vector<VGMItem*> PianoRollView::uniqueItemsForNoteIndices(const std::vector<size_t>& indices) const {
  std::vector<VGMItem*> items;
  items.reserve(indices.size());

  std::unordered_set<VGMItem*> seenItems;
  seenItems.reserve(indices.size() * 2 + 1);

  for (size_t index : indices) {
    if (index >= m_selectableNotes.size()) {
      continue;
    }
    VGMItem* item = m_selectableNotes[index].item;
    if (!item || !seenItems.insert(item).second) {
      continue;
    }
    items.push_back(item);
  }

  return items;
}

void PianoRollView::rebuildSelectedNotesCache() {
  std::vector<PianoRollFrame::Note> selectedNotes;
  selectedNotes.reserve(m_selectedNoteIndices.size());
  for (size_t selectedIndex : m_selectedNoteIndices) {
    const auto& selected = m_selectableNotes[selectedIndex];
    selectedNotes.push_back({
        selected.startTick,
        selected.duration,
        selected.key,
        selected.trackIndex,
    });
  }
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>(std::move(selectedNotes));
}

void PianoRollView::previewSingleNoteAtViewportPoint(const QPoint& pos) {
  const int noteIndex = noteIndexAtViewportPoint(pos);
  if (noteIndex < 0 || noteIndex >= static_cast<int>(m_selectableNotes.size())) {
    clearPreviewNotes();
    return;
  }

  const size_t index = static_cast<size_t>(noteIndex);
  applyPreviewNoteIndices({index}, index, static_cast<int>(m_selectableNotes[index].startTick));
}

void PianoRollView::applySelectedNoteIndices(std::vector<size_t> indices,
                                             bool emitSelectionSignal,
                                             VGMItem* preferredPrimary) {
  normalizeNoteIndices(indices);
  size_t filteredCount = 0;
  for (size_t index : indices) {
    if (index >= m_selectableNotes.size()) {
      continue;
    }
    if (!isTrackEnabled(m_selectableNotes[index].trackIndex)) {
      continue;
    }
    indices[filteredCount++] = index;
  }
  indices.resize(filteredCount);

  const bool selectionSetWasChanged = (indices != m_selectedNoteIndices);
  m_selectedNoteIndices = std::move(indices);
  rebuildSelectedNotesCache();

  VGMItem* nextPrimary = nullptr;
  if (isSelectedItem(preferredPrimary)) {
    nextPrimary = preferredPrimary;
  } else if (isSelectedItem(m_primarySelectedItem)) {
    nextPrimary = m_primarySelectedItem;
  } else if (!m_selectedNoteIndices.empty()) {
    nextPrimary = m_selectableNotes[m_selectedNoteIndices.front()].item;
  }

  const bool primaryChanged = (nextPrimary != m_primarySelectedItem);
  m_primarySelectedItem = nextPrimary;

  if (selectionSetWasChanged) {
    requestRender();
  }

  if (emitSelectionSignal && (selectionSetWasChanged || primaryChanged)) {
    emitCurrentSelectionSignals();
  }
}

void PianoRollView::emitCurrentSelectionSignals() {
  emit selectionChanged(m_primarySelectedItem);
  std::vector<VGMItem*> selectedItems = uniqueItemsForNoteIndices(m_selectedNoteIndices);
  emit selectionSetChanged(selectedItems, m_primarySelectedItem);
}

void PianoRollView::updateMarqueeSelection(bool emitSelectionSignal) {
  if (m_selectableNotes.empty()) {
    applySelectedNoteIndices({}, emitSelectionSignal, nullptr);
    return;
  }

  // Build selection in world coordinates so drag + auto-scroll keeps a stable content anchor.
  const QPoint anchorWorld = m_noteSelectionAnchorWorldValid
                                 ? m_noteSelectionAnchorWorld
                                 : graphWorldPosFromViewport(m_noteSelectionAnchor);
  const QPoint currentWorld = graphWorldPosFromViewport(m_noteSelectionCurrent);

  const float worldXMin = static_cast<float>(std::min(anchorWorld.x(), currentWorld.x()));
  const float worldXMax = static_cast<float>(std::max(anchorWorld.x(), currentWorld.x()));
  const float worldYMin = static_cast<float>(std::min(anchorWorld.y(), currentWorld.y()));
  const float worldYMax = static_cast<float>(std::max(anchorWorld.y(), currentWorld.y()));
  if (worldXMax <= worldXMin || worldYMax <= worldYMin) {
    applySelectedNoteIndices({}, emitSelectionSignal, nullptr);
    return;
  }

  const float clampedWorldXMin = std::max(0.0f, worldXMin);
  const float clampedWorldXMax = std::max(0.0f, worldXMax);
  const float clampedWorldYMin = std::max(0.0f, worldYMin);
  const float clampedWorldYMax = std::max(0.0f, worldYMax);
  if (clampedWorldXMax <= clampedWorldXMin || clampedWorldYMax <= clampedWorldYMin) {
    applySelectedNoteIndices({}, emitSelectionSignal, nullptr);
    return;
  }

  const float pixelsPerTick = std::max(0.0001f, m_pixelsPerTick);
  const float pixelsPerKey = std::max(0.0001f, m_pixelsPerKey);

  const uint64_t tickMin = static_cast<uint64_t>(std::floor(clampedWorldXMin / pixelsPerTick));
  const uint64_t tickMax = static_cast<uint64_t>(std::ceil(clampedWorldXMax / pixelsPerTick));
  const uint64_t maxDurationTicks = std::max<uint64_t>(1u, m_maxNoteDurationTicks);
  // Include earlier start ticks so long notes crossing into the marquee are still considered.
  const uint64_t searchStartTick = (tickMin > maxDurationTicks) ? (tickMin - maxDurationTicks) : 0u;
  const uint64_t searchEndTickExclusive = tickMax + 1u;

  const auto beginIt = std::lower_bound(
      m_selectableNotes.begin(),
      m_selectableNotes.end(),
      searchStartTick,
      [](const SelectableNote& note, uint64_t tick) {
        return static_cast<uint64_t>(note.startTick) < tick;
      });
  const auto endIt = std::lower_bound(
      m_selectableNotes.begin(),
      m_selectableNotes.end(),
      searchEndTickExclusive,
      [](const SelectableNote& note, uint64_t tick) {
        return static_cast<uint64_t>(note.startTick) < tick;
      });

  std::vector<size_t> indices;
  indices.reserve(static_cast<size_t>(std::max<std::ptrdiff_t>(0, std::distance(beginIt, endIt))));

  for (auto it = beginIt; it != endIt; ++it) {
    if (!isTrackEnabled(it->trackIndex)) {
      continue;
    }
    const float noteStartX = static_cast<float>(it->startTick) * pixelsPerTick;
    const float noteEndX = static_cast<float>(it->startTick + std::max<uint32_t>(1u, it->duration)) * pixelsPerTick;
    if (noteEndX <= clampedWorldXMin || noteStartX >= clampedWorldXMax) {
      continue;
    }

    const float noteTopY = (127.0f - static_cast<float>(it->key)) * pixelsPerKey;
    const float noteBottomY = noteTopY + std::max(1.0f, pixelsPerKey - 1.0f);
    if (noteBottomY <= clampedWorldYMin || noteTopY >= clampedWorldYMax) {
      continue;
    }

    indices.push_back(static_cast<size_t>(std::distance(m_selectableNotes.begin(), it)));
  }

  applySelectedNoteIndices(std::move(indices), emitSelectionSignal, nullptr);
}

void PianoRollView::updateMarqueePreview(const QPoint& cursorPos) {
  const auto& currentSelection = m_selectedNoteIndices;
  if (currentSelection.empty()) {
    clearPreviewNotes();
    return;
  }

  const int localX = std::max(0, cursorPos.x() - kKeyboardWidth);
  const float frontierX = static_cast<float>(horizontalScrollBar()->value() + localX);
  const float pixelsPerTick = std::max(0.0001f, m_pixelsPerTick);
  const float frontierTick = frontierX / pixelsPerTick;
  const int previewTick = static_cast<int>(std::max(0.0f, std::floor(frontierTick)));

  std::vector<size_t> overlapIndices;
  overlapIndices.reserve(currentSelection.size());

  for (size_t selectedIndex : currentSelection) {
    const auto& note = m_selectableNotes[selectedIndex];
    const float noteStartTick = static_cast<float>(note.startTick);
    const float noteEndTick = noteStartTick + static_cast<float>(std::max<uint32_t>(1u, note.duration));

    if (frontierTick >= noteStartTick && frontierTick < noteEndTick) {
      overlapIndices.push_back(selectedIndex);
    }
  }

  if (!overlapIndices.empty()) {
    const size_t anchorIndex = overlapIndices.front();
    applyPreviewNoteIndices(std::move(overlapIndices), anchorIndex, previewTick);
    return;
  }

  clearPreviewNotes();
}

void PianoRollView::applyPreviewNoteIndices(std::vector<size_t> indices, size_t anchorIndex, int previewTick) {
  normalizeNoteIndices(indices);

  if (indices.empty()) {
    if (m_previewNoteIndices.empty()) {
      return;
    }
    m_previewNoteIndices.clear();
    m_previewAnchorNoteIndex = kInvalidNoteIndex;
    emit notePreviewStopped();
    return;
  }

  if (anchorIndex >= m_selectableNotes.size() ||
      !std::binary_search(indices.begin(), indices.end(), anchorIndex)) {
    anchorIndex = indices.front();
  }

  if (indices == m_previewNoteIndices && anchorIndex == m_previewAnchorNoteIndex) {
    return;
  }

  m_previewNoteIndices = std::move(indices);
  m_previewAnchorNoteIndex = anchorIndex;

  std::vector<PreviewSelection> previewNotes;
  previewNotes.reserve(m_previewNoteIndices.size());
  for (size_t index : m_previewNoteIndices) {
    const auto& selected = m_selectableNotes[index];
    if (!selected.item) {
      continue;
    }
    previewNotes.push_back({selected.item, selected.key});
  }

  if (previewNotes.empty()) {
    m_previewNoteIndices.clear();
    m_previewAnchorNoteIndex = kInvalidNoteIndex;
    emit notePreviewStopped();
    return;
  }

  emit notePreviewRequested(previewNotes, std::max(0, previewTick));
}

void PianoRollView::clearPreviewNotes() {
  applyPreviewNoteIndices({}, kInvalidNoteIndex, -1);
}

int PianoRollView::noteIndexAtViewportPoint(const QPoint& pos) const {
  if (pos.x() < kKeyboardWidth || pos.y() < kTopBarHeight || m_selectableNotes.empty()) {
    return -1;
  }

  if (m_pixelsPerTick <= 0.0f || m_pixelsPerKey <= 0.0f) {
    return -1;
  }

  const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
  const int noteViewportHeight = std::max(0, viewport()->height() - kTopBarHeight);
  const int localX = pos.x() - kKeyboardWidth;
  const int localY = pos.y() - kTopBarHeight;
  if (localX < 0 || localY < 0 || localX >= noteViewportWidth || localY >= noteViewportHeight) {
    return -1;
  }

  const float worldX = static_cast<float>(horizontalScrollBar()->value() + localX);
  const float worldY = static_cast<float>(verticalScrollBar()->value() + localY);
  const float pixelsPerKey = std::max(0.0001f, m_pixelsPerKey);
  const int rowFromTop = static_cast<int>(std::floor(worldY / pixelsPerKey));
  if (rowFromTop < 0 || rowFromTop >= kMidiKeyCount) {
    return -1;
  }

  const float rowTopY = static_cast<float>(rowFromTop) * pixelsPerKey;
  const float rowBodyHeight = std::max(1.0f, pixelsPerKey - 1.0f);
  if (worldY < rowTopY || worldY >= (rowTopY + rowBodyHeight)) {
    return -1;
  }

  const int key = 127 - rowFromTop;
  const float worldTick = worldX / std::max(0.0001f, m_pixelsPerTick);
  const uint64_t tickFloor = (worldTick <= 0.0f)
                                 ? 0u
                                 : static_cast<uint64_t>(std::floor(worldTick));
  const uint64_t maxDurationTicks = std::max<uint64_t>(1u, m_maxNoteDurationTicks);
  const uint64_t searchStartTick = (tickFloor > maxDurationTicks)
                                       ? (tickFloor - maxDurationTicks)
                                       : 0u;
  const uint64_t searchEndTickExclusive = std::min<uint64_t>(
      tickFloor + 2u, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1u);

  const auto beginIt = std::lower_bound(
      m_selectableNotes.begin(),
      m_selectableNotes.end(),
      searchStartTick,
      [](const SelectableNote& note, uint64_t tick) {
        return static_cast<uint64_t>(note.startTick) < tick;
      });
  const auto endIt = std::lower_bound(
      m_selectableNotes.begin(),
      m_selectableNotes.end(),
      searchEndTickExclusive,
      [](const SelectableNote& note, uint64_t tick) {
        return static_cast<uint64_t>(note.startTick) < tick;
      });

  int bestNoteIndex = -1;
  uint32_t bestStartTick = 0;
  int bestTrackIndex = std::numeric_limits<int>::min();

  for (auto it = beginIt; it != endIt; ++it) {
    if (it->key != key || !it->item || !isTrackEnabled(it->trackIndex)) {
      continue;
    }

    const float noteStartTick = static_cast<float>(it->startTick);
    const float noteEndTick =
        noteStartTick + static_cast<float>(std::max<uint32_t>(1u, it->duration));
    if (worldTick < noteStartTick || worldTick >= noteEndTick) {
      continue;
    }

    const bool betterMatch =
        (bestNoteIndex < 0) ||
        (it->startTick > bestStartTick) ||
        (it->startTick == bestStartTick && it->trackIndex >= bestTrackIndex);
    if (betterMatch) {
      bestNoteIndex = static_cast<int>(std::distance(m_selectableNotes.begin(), it));
      bestStartTick = it->startTick;
      bestTrackIndex = it->trackIndex;
    }
  }

  return bestNoteIndex;
}

VGMItem* PianoRollView::noteAtViewportPoint(const QPoint& pos) const {
  const int noteIndex = noteIndexAtViewportPoint(pos);
  if (noteIndex < 0 || noteIndex >= static_cast<int>(m_selectableNotes.size())) {
    return nullptr;
  }
  return m_selectableNotes[static_cast<size_t>(noteIndex)].item;
}

void PianoRollView::zoomHorizontalFromButton(int steps) {
  if (steps == 0) {
    return;
  }

  const int playheadX = kKeyboardWidth + static_cast<int>(std::lround(
      visualPlaybackTick() * std::max(0.0001f, m_pixelsPerTick) -
      static_cast<float>(horizontalScrollBar()->value())));
  const int anchorX = (playheadX >= kKeyboardWidth && playheadX < viewport()->width())
      ? playheadX
      : (viewport()->width() / 2);
  zoomHorizontal(steps, anchorX, true, 150);
}

void PianoRollView::zoomHorizontal(int steps, int anchorX, bool animated, int durationMs) {
  if (steps == 0) {
    return;
  }

  const float factor = std::pow(1.15f, static_cast<float>(steps));
  zoomHorizontalFactor(factor, anchorX, animated, durationMs);
}

void PianoRollView::zoomVertical(int steps, int anchorY, bool animated, int durationMs) {
  if (steps == 0) {
    return;
  }

  const float factor = std::pow(1.15f, static_cast<float>(steps));
  zoomVerticalFactor(factor, anchorY, animated, durationMs);
}

void PianoRollView::zoomHorizontalFactor(float factor, int anchorX, bool animated, int durationMs) {
  const float clampedFactor = std::clamp(factor, 0.05f, 20.0f);
  const float targetScale = std::clamp(m_pixelsPerTick * clampedFactor, kMinPixelsPerTick, kMaxPixelsPerTick);
  if (std::abs(targetScale - m_pixelsPerTick) < 0.0001f) {
    return;
  }

  if (animated) {
    animateHorizontalScale(targetScale, anchorX, durationMs);
  } else {
    // Keep the world position under the cursor fixed while changing scale.
    const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
    const int anchorInNotes = std::clamp(anchorX - kKeyboardWidth, 0, noteViewportWidth);
    const float worldTickAtAnchor = static_cast<float>(horizontalScrollBar()->value() + anchorInNotes) /
                                    std::max(0.0001f, m_pixelsPerTick);
    applyHorizontalScale(targetScale, anchorInNotes, worldTickAtAnchor);
  }
}

void PianoRollView::zoomVerticalFactor(float factor, int anchorY, bool animated, int durationMs) {
  const float clampedFactor = std::clamp(factor, 0.05f, 20.0f);
  const float targetScale = std::clamp(m_pixelsPerKey * clampedFactor, kMinPixelsPerKey, kMaxPixelsPerKey);
  if (std::abs(targetScale - m_pixelsPerKey) < 0.0001f) {
    return;
  }

  if (animated) {
    animateVerticalScale(targetScale, anchorY, durationMs);
  } else {
    // Keep the world Y under the cursor fixed while changing vertical scale.
    const int noteViewportHeight = std::max(0, viewport()->height() - kTopBarHeight);
    const int anchorInNotes = std::clamp(anchorY - kTopBarHeight, 0, noteViewportHeight);
    const float worldYAtAnchor = static_cast<float>(verticalScrollBar()->value() + anchorInNotes) /
                                 std::max(0.0001f, m_pixelsPerKey);
    applyVerticalScale(targetScale, anchorInNotes, worldYAtAnchor);
  }
}

void PianoRollView::applyHorizontalScale(float scale, int anchorInNotes, float worldTickAtAnchor) {
  m_pixelsPerTick = std::clamp(scale, kMinPixelsPerTick, kMaxPixelsPerTick);
  updateScrollBars();

  const int newValue = static_cast<int>(std::llround(worldTickAtAnchor * m_pixelsPerTick - anchorInNotes));
  m_applyingHorizontalZoomScroll = true;
  horizontalScrollBar()->setValue(std::clamp(newValue,
                                             horizontalScrollBar()->minimum(),
                                             horizontalScrollBar()->maximum()));
  m_applyingHorizontalZoomScroll = false;
  requestRender();
}

void PianoRollView::applyVerticalScale(float scale, int anchorInNotes, float worldYAtAnchor) {
  m_pixelsPerKey = std::clamp(scale, kMinPixelsPerKey, kMaxPixelsPerKey);
  updateScrollBars();

  const int newValue = static_cast<int>(std::llround(worldYAtAnchor * m_pixelsPerKey - anchorInNotes));
  verticalScrollBar()->setValue(std::clamp(newValue,
                                           verticalScrollBar()->minimum(),
                                           verticalScrollBar()->maximum()));
  requestRender();
}

void PianoRollView::animateHorizontalScale(float targetScale, int anchorX, int durationMs) {
  if (!m_horizontalZoomAnimation) {
    return;
  }

  const int noteViewportWidth = std::max(0, viewport()->width() - kKeyboardWidth);
  m_horizontalZoomAnchor = std::clamp(anchorX - kKeyboardWidth, 0, noteViewportWidth);
  m_horizontalZoomWorldTick = static_cast<float>(horizontalScrollBar()->value() + m_horizontalZoomAnchor) /
                              std::max(0.0001f, m_pixelsPerTick);

  if (m_horizontalZoomAnimation->state() == QAbstractAnimation::Running) {
    m_horizontalZoomAnimation->stop();
  }
  m_horizontalZoomAnimation->setDuration(std::max(1, durationMs));
  m_horizontalZoomAnimation->setStartValue(m_pixelsPerTick);
  m_horizontalZoomAnimation->setEndValue(targetScale);
  m_horizontalZoomAnimation->start();
}

void PianoRollView::animateVerticalScale(float targetScale, int anchorY, int durationMs) {
  if (!m_verticalZoomAnimation) {
    return;
  }

  const int noteViewportHeight = std::max(0, viewport()->height() - kTopBarHeight);
  m_verticalZoomAnchor = std::clamp(anchorY - kTopBarHeight, 0, noteViewportHeight);
  m_verticalZoomWorldY = static_cast<float>(verticalScrollBar()->value() + m_verticalZoomAnchor) /
                         std::max(0.0001f, m_pixelsPerKey);

  if (m_verticalZoomAnimation->state() == QAbstractAnimation::Running) {
    m_verticalZoomAnimation->stop();
  }
  m_verticalZoomAnimation->setDuration(std::max(1, durationMs));
  m_verticalZoomAnimation->setStartValue(m_pixelsPerKey);
  m_verticalZoomAnimation->setEndValue(targetScale);
  m_verticalZoomAnimation->start();
}
