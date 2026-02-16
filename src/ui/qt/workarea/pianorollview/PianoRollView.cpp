/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollView.h"

#include "PianoRollRhiHost.h"
#include "PianoRollZoomScrollBar.h"

#include "SeqEvent.h"
#include "SeqEventTimeIndex.h"
#include "SeqTrack.h"
#include "VGMSeq.h"
#include "util/NonTransientScrollBarStyle.h"

#include <QAbstractAnimation>
#include <QEvent>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPalette>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

PianoRollView::PianoRollView(QWidget* parent)
    : QAbstractScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);

  auto* hbar = new PianoRollZoomScrollBar(Qt::Horizontal, this);
  auto* vbar = new PianoRollZoomScrollBar(Qt::Vertical, this);
  setHorizontalScrollBar(hbar);
  setVerticalScrollBar(vbar);

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

#ifdef Q_OS_MAC
  // Match HexView behavior so scrollbars remain visible above native RHI windows.
  QtUi::applyNonTransientScrollBarStyle(hbar);
  QtUi::applyNonTransientScrollBarStyle(vbar);
#endif

  connect(hbar, &PianoRollZoomScrollBar::zoomInRequested, this, [this]() {
    // Zoom from viewport center when using scrollbar +/- buttons.
    zoomHorizontal(+1, viewport()->width() / 2, true, 150);
  });
  connect(hbar, &PianoRollZoomScrollBar::zoomOutRequested, this, [this]() {
    zoomHorizontal(-1, viewport()->width() / 2, true, 150);
  });

  connect(vbar, &PianoRollZoomScrollBar::zoomInRequested, this, [this]() {
    zoomVertical(+1, viewport()->height() / 2, true, 150);
  });
  connect(vbar, &PianoRollZoomScrollBar::zoomOutRequested, this, [this]() {
    zoomVertical(-1, viewport()->height() / 2, true, 150);
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

  m_rhiHost = new PianoRollRhiHost(this, viewport());
  // Host owns whichever render surface backend is active on this platform.
  m_rhiHost->setGeometry(viewport()->rect());
  m_rhiHost->setFocusPolicy(Qt::NoFocus);
  m_rhiHost->setMouseTracking(true);

  m_notes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_activeNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_selectedNotes = std::make_shared<std::vector<PianoRollFrame::Note>>();
  m_timeSignatures = std::make_shared<std::vector<PianoRollFrame::TimeSignature>>();

  for (auto& state : m_activeKeys) {
    state.trackIndex = -1;
    state.startTick = 0;
  }

  m_animClock.start();
  m_renderClock.start();
  updateScrollBars();
}

void PianoRollView::setSequence(VGMSeq* seq) {
  if (seq == m_seq) {
    return;
  }

  clearPreviewNotes();

  m_seq = seq;
  m_timeline = nullptr;
  m_timelineCursor.reset();
  m_attemptedTimelineBuild = false;
  m_noteSelectionPressActive = false;
  m_noteSelectionDragging = false;
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

void PianoRollView::setPlaybackTick(int tick, bool playbackActive) {
  tick = clampTick(tick);
  const bool tickChanged = (tick != m_currentTick);
  const bool playbackStateChanged = (playbackActive != m_playbackActive);
  if (!tickChanged && !playbackStateChanged) {
    return;
  }

  m_currentTick = tick;
  m_playbackActive = playbackActive;

  const bool activeKeysChanged = updateActiveKeyStates();
  const int scanX = scanlinePixelX(tick);
  const bool scanlineChanged = (scanX != m_lastRenderedScanlineX);
  if (playbackStateChanged || activeKeysChanged || scanlineChanged) {
    m_lastRenderedScanlineX = scanX;
    requestRender();
  }
}

void PianoRollView::clearPlaybackState() {
  m_playbackActive = false;
  m_currentTick = 0;
  updateActiveKeyStates();
  m_lastRenderedScanlineX = std::numeric_limits<int>::min();
  requestRender();
}

PianoRollFrame::Data PianoRollView::captureRhiFrameData(float dpr) const {
  PianoRollFrame::Data frame;
  frame.viewportSize = viewport()->size();
  frame.dpr = std::max(1.0f, dpr);

  frame.totalTicks = m_totalTicks;
  frame.currentTick = clampTick(m_currentTick);
  frame.trackCount = m_trackCount;
  frame.ppqn = std::max(1, m_ppqn);
  frame.maxNoteDurationTicks = std::max<uint32_t>(1, m_maxNoteDurationTicks);

  frame.scrollX = horizontalScrollBar()->value();
  frame.scrollY = verticalScrollBar()->value();
  frame.keyboardWidth = kKeyboardWidth;
  frame.topBarHeight = kTopBarHeight;
  frame.pixelsPerTick = m_pixelsPerTick;
  frame.pixelsPerKey = m_pixelsPerKey;
  frame.elapsedSeconds = static_cast<float>(m_animClock.elapsed()) / 1000.0f;
  frame.playbackActive = m_playbackActive;

  frame.trackColors = m_trackColors;
  frame.notes = m_notes;
  frame.activeNotes = m_activeNotes;
  frame.selectedNotes = m_selectedNotes;
  frame.timeSignatures = m_timeSignatures;

  frame.activeKeyTrack.fill(-1);
  for (int key = 0; key < kMidiKeyCount; ++key) {
    frame.activeKeyTrack[static_cast<size_t>(key)] = m_activeKeys[static_cast<size_t>(key)].trackIndex;
  }

  const QPalette pal = palette();
  const QColor base = pal.color(QPalette::Base);
  const QColor window = pal.color(QPalette::Window);
  const QColor text = pal.color(QPalette::Text);
  const bool dark = base.lightnessF() < 0.5f;

  frame.backgroundColor = window;
  frame.noteBackgroundColor = dark ? base.lighter(108) : base;
  frame.keyboardBackgroundColor = dark ? QColor(31, 35, 40) : QColor(219, 223, 228);
  frame.topBarBackgroundColor = dark ? QColor(47, 52, 60) : QColor(210, 216, 224);
  frame.topBarProgressColor = dark ? QColor(108, 125, 158, 85) : QColor(100, 129, 180, 70);
  frame.measureLineColor = dark ? QColor(185, 196, 216, 125) : QColor(88, 102, 128, 96);
  frame.beatLineColor = dark ? QColor(162, 169, 184, 62) : QColor(102, 112, 132, 54);
  frame.keySeparatorColor = dark ? QColor(20, 24, 28, 140) : QColor(108, 116, 128, 70);
  frame.noteOutlineColor = dark ? QColor(18, 20, 22, 180) : QColor(62, 70, 84, 130);
  frame.scanLineColor = dark ? QColor(255, 94, 77, 230) : QColor(201, 56, 36, 220);
  frame.whiteKeyColor = dark ? QColor(188, 193, 202) : QColor(242, 244, 247);
  frame.blackKeyColor = dark ? QColor(58, 64, 74) : QColor(86, 93, 104);
  frame.whiteKeyRowColor = dark ? QColor(76, 84, 95, 42) : QColor(98, 116, 138, 24);
  frame.blackKeyRowColor = dark ? QColor(44, 50, 59, 68) : QColor(92, 106, 126, 34);
  frame.dividerColor = dark ? text.lighter(115) : text.darker(125);
  frame.dividerColor.setAlpha(dark ? 74 : 62);
  frame.selectedNoteFillColor = dark ? QColor(72, 182, 255, 72) : QColor(52, 148, 225, 66);
  frame.selectedNoteOutlineColor = dark ? QColor(138, 214, 255, 245) : QColor(36, 110, 196, 230);
  frame.selectionRectFillColor = dark ? QColor(78, 184, 255, 42) : QColor(72, 158, 232, 40);
  frame.selectionRectOutlineColor = dark ? QColor(138, 214, 255, 212) : QColor(36, 110, 196, 205);

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

void PianoRollView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  if (m_rhiHost) {
    m_rhiHost->setGeometry(viewport()->rect());
  }
  updateScrollBars();
  if (!m_initialViewportPositioned) {
    scheduleViewportSync();
  }
  requestRender();
}

void PianoRollView::showEvent(QShowEvent* event) {
  QAbstractScrollArea::showEvent(event);
  if (!m_initialViewportPositioned) {
    scheduleViewportSync();
  } else {
    updateScrollBars();
    requestRender();
  }
}

void PianoRollView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  Q_UNUSED(dx);
  Q_UNUSED(dy);
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
    requestRender();
  }
}

int PianoRollView::noteKeyForEvent(const SeqEvent* event) {
  if (!event) {
    return -1;
  }
  if (auto* noteOn = dynamic_cast<const NoteOnSeqEvent*>(event)) {
    return noteOn->absKey;
  }
  if (auto* durNote = dynamic_cast<const DurNoteSeqEvent*>(event)) {
    return durNote->absKey;
  }
  return -1;
}

QColor PianoRollView::colorForTrack(int trackIndex) const {
  const int hue = (trackIndex * 43) % 360;
  return QColor::fromHsv(hue, 190, 235);
}

bool PianoRollView::handleViewportWheel(QWheelEvent* event) {
  if (!event) {
    return false;
  }

  const Qt::KeyboardModifiers mods = event->modifiers();
  const bool zoomHorizontalOnly = mods.testFlag(Qt::ControlModifier);
  const bool zoomVerticalOnly = mods.testFlag(Qt::AltModifier);
  if (!zoomHorizontalOnly && !zoomVerticalOnly) {
    return false;
  }

  int delta = event->angleDelta().y();
  if (delta == 0) {
    delta = event->angleDelta().x();
  }
  if (delta == 0) {
    delta = event->pixelDelta().y();
  }
  if (delta == 0) {
    delta = event->pixelDelta().x();
  }

  int steps = 0;
  if (delta > 0) {
    steps = std::max(1, delta / 120);
  } else if (delta < 0) {
    steps = std::min(-1, delta / 120);
  }
  if (steps == 0) {
    steps = (delta >= 0) ? 1 : -1;
  }

  const QPoint pos = event->position().toPoint();
  if (zoomHorizontalOnly) {
    zoomHorizontal(steps, pos.x(), false, 0);
  }
  if (zoomVerticalOnly) {
    zoomVertical(steps, pos.y(), false, 0);
  }

  event->accept();
  return true;
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

  if (modifiers.testFlag(Qt::AltModifier)) {
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

  const QPoint pos = event->position().toPoint();
  if (pos.y() < kTopBarHeight && pos.x() >= kKeyboardWidth) {
    clearPreviewNotes();
    m_noteSelectionPressActive = false;
    m_noteSelectionDragging = false;
    m_seekDragActive = true;
    m_currentTick = tickFromViewportX(pos.x());
    updateActiveKeyStates();
    requestRender();
    emit seekRequested(m_currentTick);

    event->accept();
    return true;
  }

  if (pos.y() >= kTopBarHeight && pos.x() >= kKeyboardWidth) {
    m_noteSelectionPressActive = true;
    m_noteSelectionDragging = false;
    m_noteSelectionAnchor = pos;
    m_noteSelectionCurrent = pos;
    previewSingleNoteAtViewportPoint(pos);

    event->accept();
    return true;
  }

  return false;
}

bool PianoRollView::handleViewportMouseMove(QMouseEvent* event) {
  if (!event) {
    return false;
  }

  if (m_seekDragActive) {
    const QPoint pos = event->position().toPoint();
    m_currentTick = tickFromViewportX(pos.x());
    updateActiveKeyStates();
    requestRender();
    emit seekRequested(m_currentTick);

    event->accept();
    return true;
  }

  if (!m_noteSelectionPressActive) {
    return false;
  }

  if (!(event->buttons() & Qt::LeftButton)) {
    m_noteSelectionPressActive = false;
    m_noteSelectionDragging = false;
    clearPreviewNotes();
    requestRender();
    event->accept();
    return true;
  }

  const QPoint pos = event->position().toPoint();
  m_noteSelectionCurrent = pos;
  const QPoint dragDelta = m_noteSelectionCurrent - m_noteSelectionAnchor;
  const bool movedEnough = std::abs(dragDelta.x()) >= 2 || std::abs(dragDelta.y()) >= 2;
  if (!m_noteSelectionDragging && movedEnough) {
    m_noteSelectionDragging = true;
  }

  if (m_noteSelectionDragging) {
    updateMarqueeSelection(false);
    updateMarqueePreview(pos);
    requestRenderCoalesced();
  } else {
    previewSingleNoteAtViewportPoint(pos);
  }

  event->accept();
  return true;
}

bool PianoRollView::handleViewportMouseRelease(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    return false;
  }

  if (m_seekDragActive) {
    const QPoint pos = event->position().toPoint();
    m_currentTick = tickFromViewportX(pos.x());
    m_seekDragActive = false;
    updateActiveKeyStates();
    requestRender();

    event->accept();
    return true;
  }

  if (!m_noteSelectionPressActive) {
    return false;
  }

  const QPoint pos = event->position().toPoint();
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
  if (wasDragging) {
    requestRender();
  }

  event->accept();
  return true;
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

void PianoRollView::rebuildTrackColors() {
  m_trackColors.resize(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i) {
    m_trackColors[static_cast<size_t>(i)] = colorForTrack(i);
  }
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

    int noteKey = noteKeyForEvent(timed.event);
    if (noteKey >= 0) {
      noteKey += m_seq->transposeTimeline().totalTransposeForTimedEvent(i);
    }
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
  std::array<ActiveKeyState, kMidiKeyCount> nextActiveKeys{};
  for (auto& state : nextActiveKeys) {
    state.trackIndex = -1;
    state.startTick = 0;
  }
  std::vector<PianoRollFrame::Note> nextActiveNotes;

  if (m_timeline && m_timelineCursor && m_timeline->finalized()) {
    std::vector<const SeqTimedEvent*> active;
    m_timelineCursor->getActiveAt(static_cast<uint32_t>(std::max(0, m_currentTick)), active);
    nextActiveNotes.reserve(active.size());

    for (const auto* timed : active) {
      if (!timed || !timed->event) {
        continue;
      }

      int key = -1;
      const auto keyIt = m_transposedKeyByTimedEvent.find(timed);
      if (keyIt != m_transposedKeyByTimedEvent.end()) {
        key = keyIt->second;
      } else {
        key = noteKeyForEvent(timed->event);
        if (m_seq && key >= 0) {
          key += m_seq->transposeTimeline().totalTransposeForTimedEvent(timed);
        }
      }
      if (key < 0 || key >= kMidiKeyCount) {
        continue;
      }

      const int trackIndex = trackIndexForEvent(timed->event);
      if (trackIndex < 0) {
        continue;
      }

      auto& state = nextActiveKeys[static_cast<size_t>(key)];
      // If multiple tracks hold the same key, use the most recent note start.
      if (state.trackIndex < 0 || timed->startTick >= state.startTick) {
        state.trackIndex = trackIndex;
        state.startTick = timed->startTick;
      }

      nextActiveNotes.push_back({
          timed->startTick,
          std::max<uint32_t>(1, timed->duration),
          static_cast<uint8_t>(key),
          static_cast<int16_t>(trackIndex),
      });
    }
  }

  std::sort(nextActiveNotes.begin(), nextActiveNotes.end(), [](const auto& a, const auto& b) {
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
      if (oldNote.startTick != newNote.startTick ||
          oldNote.duration != newNote.duration ||
          oldNote.key != newNote.key ||
          oldNote.trackIndex != newNote.trackIndex) {
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

  if (!m_initialViewportPositioned) {
    // QStackedWidget can report a tiny pre-layout viewport (e.g. ~28px) when hidden.
    // Defer initial centering until dimensions are actually usable.
    const bool usableViewport = noteViewportWidth >= 64 && noteViewportHeight >= 64;
    if (usableViewport) {
      // Default initial focus: center of the keyboard range.
      verticalScrollBar()->setValue(verticalScrollBar()->maximum() / 2);
      m_initialViewportPositioned = true;
    } else {
      scheduleViewportSync();
    }
  }

  m_currentTick = clampTick(m_currentTick);
}

void PianoRollView::scheduleViewportSync() {
  if (m_viewportSyncQueued) {
    return;
  }

  m_viewportSyncQueued = true;
  QTimer::singleShot(0, this, [this]() {
    m_viewportSyncQueued = false;
    if (!isVisible()) {
      return;
    }
    updateScrollBars();
    requestRender();
  });
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
  // Clamp high-frequency scroll updates to roughly one render per refresh interval.
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

  const float leftLimit = static_cast<float>(graphRect.left());
  const float topLimit = static_cast<float>(graphRect.top());
  const float rightLimit = leftLimit + static_cast<float>(graphRect.width());
  const float bottomLimit = topLimit + static_cast<float>(graphRect.height());

  const float anchorX = std::clamp(static_cast<float>(m_noteSelectionAnchor.x()), leftLimit, rightLimit);
  const float anchorY = std::clamp(static_cast<float>(m_noteSelectionAnchor.y()), topLimit, bottomLimit);
  const float currentX = std::clamp(static_cast<float>(m_noteSelectionCurrent.x()), leftLimit, rightLimit);
  const float currentY = std::clamp(static_cast<float>(m_noteSelectionCurrent.y()), topLimit, bottomLimit);

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

  const QRectF marquee = graphSelectionRectInViewport();
  if (marquee.width() <= 0.5f || marquee.height() <= 0.5f) {
    applySelectedNoteIndices({}, emitSelectionSignal, nullptr);
    return;
  }

  const float worldXMin = static_cast<float>(horizontalScrollBar()->value()) + (marquee.left() - static_cast<float>(kKeyboardWidth));
  const float worldXMax = static_cast<float>(horizontalScrollBar()->value()) + (marquee.right() - static_cast<float>(kKeyboardWidth));
  const float worldYMin = static_cast<float>(verticalScrollBar()->value()) + (marquee.top() - static_cast<float>(kTopBarHeight));
  const float worldYMax = static_cast<float>(verticalScrollBar()->value()) + marquee.bottom() - static_cast<float>(kTopBarHeight);
  if (worldXMax <= worldXMin || worldYMax <= worldYMin) {
    applySelectedNoteIndices({}, emitSelectionSignal, nullptr);
    return;
  }

  const float pixelsPerTick = std::max(0.0001f, m_pixelsPerTick);
  const float pixelsPerKey = std::max(0.0001f, m_pixelsPerKey);

  const uint64_t tickMin = static_cast<uint64_t>(std::max(0.0f, std::floor(worldXMin / pixelsPerTick)));
  const uint64_t tickMax = static_cast<uint64_t>(std::max(0.0f, std::ceil(worldXMax / pixelsPerTick)));
  const uint64_t maxDurationTicks = std::max<uint64_t>(1u, m_maxNoteDurationTicks);
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
    const float noteStartX = static_cast<float>(it->startTick) * pixelsPerTick;
    const float noteEndX = static_cast<float>(it->startTick + std::max<uint32_t>(1u, it->duration)) * pixelsPerTick;
    if (noteEndX <= worldXMin || noteStartX >= worldXMax) {
      continue;
    }

    const float noteTopY = (127.0f - static_cast<float>(it->key)) * pixelsPerKey;
    const float noteBottomY = noteTopY + std::max(1.0f, pixelsPerKey - 1.0f);
    if (noteBottomY <= worldYMin || noteTopY >= worldYMax) {
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
    if (it->key != key || !it->item) {
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
  horizontalScrollBar()->setValue(std::clamp(newValue,
                                             horizontalScrollBar()->minimum(),
                                             horizontalScrollBar()->maximum()));
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
