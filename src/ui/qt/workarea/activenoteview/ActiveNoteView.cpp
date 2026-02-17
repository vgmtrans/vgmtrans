/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteView.h"

#include "ActiveNoteRhiHost.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>

#include <algorithm>
#include <array>
#include <cmath>

ActiveNoteView::ActiveNoteView(QWidget* parent)
    : QAbstractScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_rhiHost = new ActiveNoteRhiHost(this, viewport());
  m_rhiHost->setGeometry(viewport()->rect());
  m_rhiHost->setFocusPolicy(Qt::NoFocus);
}

ActiveNoteView::~ActiveNoteView() {
  endPreviewDrag();
  clearPreviewKey();
}

void ActiveNoteView::setTrackCount(int trackCount) {
  trackCount = std::max(0, trackCount);
  if (trackCount == m_trackCount && static_cast<int>(m_activeKeysByTrack.size()) == trackCount) {
    return;
  }

  m_trackCount = trackCount;
  m_trackColors.resize(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i) {
    m_trackColors[i] = colorForTrack(i);
  }

  const bool hadPreview = (m_previewTrackIndex >= 0 && m_previewKey >= 0);
  m_activeKeysByTrack.assign(static_cast<size_t>(m_trackCount), {});
  m_previewKeysByTrack.assign(static_cast<size_t>(m_trackCount), {});
  m_previewTrackIndex = -1;
  m_previewKey = -1;
  if (hadPreview) {
    emit notePreviewStopped();
  }
  requestRender();
}

void ActiveNoteView::setActiveNotes(const std::vector<ActiveKey>& activeKeys, bool playbackActive) {
  if (m_trackCount <= 0) {
    clearPlaybackState();
    return;
  }

  for (auto& keyMask : m_activeKeysByTrack) {
    keyMask.reset();
  }

  for (const auto& activeKey : activeKeys) {
    if (activeKey.trackIndex < 0 || activeKey.trackIndex >= m_trackCount) {
      continue;
    }
    if (activeKey.key < 0 || activeKey.key >= kPianoKeyCount) {
      continue;
    }
    m_activeKeysByTrack[static_cast<size_t>(activeKey.trackIndex)].set(static_cast<size_t>(activeKey.key));
  }

  m_playbackActive = playbackActive;
  requestRender();
}

void ActiveNoteView::clearPlaybackState() {
  m_playbackActive = false;
  for (auto& keyMask : m_activeKeysByTrack) {
    keyMask.reset();
  }
  requestRender();
}

ActiveNoteFrame::Data ActiveNoteView::captureRhiFrameData(float dpr) const {
  Q_UNUSED(dpr);

  ActiveNoteFrame::Data frame;
  frame.viewportSize = viewport()->size();
  frame.dpr = std::max(1.0f, dpr);
  frame.trackCount = m_trackCount;

  const QPalette pal = palette();
  frame.backgroundColor = pal.color(QPalette::Base);
  frame.separatorColor = pal.color(QPalette::Mid);
  frame.whiteKeyColor = QColor(236, 238, 242);
  frame.blackKeyColor = QColor(88, 93, 102);

  frame.playbackActive = m_playbackActive;
  frame.trackColors = m_trackColors;
  frame.activeKeysByTrack = m_activeKeysByTrack;
  frame.previewKeysByTrack = m_previewKeysByTrack;
  return frame;
}

void ActiveNoteView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  if (m_rhiHost) {
    m_rhiHost->setGeometry(viewport()->rect());
  }
  requestRender();
}

QColor ActiveNoteView::colorForTrack(int trackIndex) const {
  // Spread hues to keep neighboring tracks visually distinct.
  const int hue = (trackIndex * 43) % 360;
  return QColor::fromHsv(hue, 190, 235);
}

bool ActiveNoteView::isBlackKey(int key) {
  switch (key % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      return true;
    default:
      return false;
  }
}

bool ActiveNoteView::handleViewportMousePress(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    return false;
  }

  beginPreviewDrag();
  setPreviewKey(hitTestKeyboardKey(event->position().toPoint()));
  event->accept();
  return true;
}

bool ActiveNoteView::handleViewportMouseMove(QMouseEvent* event) {
  if (!event || !m_previewDragActive) {
    return false;
  }

  if (!(event->buttons() & Qt::LeftButton)) {
    endPreviewDrag();
    clearPreviewKey();
    event->accept();
    return true;
  }

  setPreviewKey(hitTestKeyboardKey(event->position().toPoint()));
  event->accept();
  return true;
}

bool ActiveNoteView::handleViewportMouseRelease(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    return false;
  }

  endPreviewDrag();
  clearPreviewKey();
  event->accept();
  return true;
}

void ActiveNoteView::beginPreviewDrag() {
  m_previewDragActive = true;
  if (!m_previewGlobalTracking && qApp) {
    qApp->installEventFilter(this);
    m_previewGlobalTracking = true;
  }
}

void ActiveNoteView::endPreviewDrag() {
  m_previewDragActive = false;
  if (m_previewGlobalTracking && qApp) {
    qApp->removeEventFilter(this);
    m_previewGlobalTracking = false;
  }
}

bool ActiveNoteView::eventFilter(QObject* watched, QEvent* event) {
  if (!m_previewDragActive || !event) {
    return QAbstractScrollArea::eventFilter(watched, event);
  }

  switch (event->type()) {
    case QEvent::MouseMove: {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const Qt::MouseButtons buttons = mouseEvent->buttons() | QApplication::mouseButtons();
      if (!(buttons & Qt::LeftButton)) {
        endPreviewDrag();
        clearPreviewKey();
        break;
      }
      if (viewport()) {
        const QPoint localPos = viewport()->mapFromGlobal(mouseEvent->globalPosition().toPoint());
        setPreviewKey(hitTestKeyboardKey(localPos));
      }
      break;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        endPreviewDrag();
        clearPreviewKey();
      }
      break;
    }
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
      endPreviewDrag();
      clearPreviewKey();
      break;
    default:
      break;
  }

  return QAbstractScrollArea::eventFilter(watched, event);
}

ActiveNoteView::KeyHitResult ActiveNoteView::hitTestKeyboardKey(const QPoint& pos) const {
  KeyHitResult hit;
  if (m_trackCount <= 0) {
    return hit;
  }

  const int viewWidth = viewport()->width();
  const int viewHeight = viewport()->height();
  if (viewWidth <= 0 || viewHeight <= 0) {
    return hit;
  }

  if (pos.x() < 0 || pos.y() < 0 || pos.x() >= viewWidth || pos.y() >= viewHeight) {
    return hit;
  }

  struct KeyGeometry {
    float x = 0.0f;
    float width = 0.0f;
    bool valid = false;
  };

  std::array<KeyGeometry, kPianoKeyCount> keyGeometry{};
  int whiteCount = 0;
  for (int key = 0; key < kPianoKeyCount; ++key) {
    if (!isBlackKey(key)) {
      ++whiteCount;
    }
  }
  if (whiteCount <= 0) {
    return hit;
  }

  const float keyboardWidth = static_cast<float>(viewWidth);
  const float whiteWidth = keyboardWidth / static_cast<float>(whiteCount);

  int whiteIndex = 0;
  for (int key = 0; key < kPianoKeyCount; ++key) {
    if (!isBlackKey(key)) {
      keyGeometry[static_cast<size_t>(key)] = {
          static_cast<float>(whiteIndex) * whiteWidth,
          whiteWidth,
          true,
      };
      ++whiteIndex;
    }
  }

  const float blackWidth = std::max(2.0f, whiteWidth * 0.62f);
  for (int key = 0; key < kPianoKeyCount; ++key) {
    if (!isBlackKey(key)) {
      continue;
    }

    int prevWhite = key - 1;
    while (prevWhite >= 0 && isBlackKey(prevWhite)) {
      --prevWhite;
    }
    int nextWhite = key + 1;
    while (nextWhite < kPianoKeyCount && isBlackKey(nextWhite)) {
      ++nextWhite;
    }
    if (prevWhite < 0 || nextWhite >= kPianoKeyCount) {
      continue;
    }
    if (!keyGeometry[static_cast<size_t>(prevWhite)].valid ||
        !keyGeometry[static_cast<size_t>(nextWhite)].valid) {
      continue;
    }

    const float splitX =
        keyGeometry[static_cast<size_t>(prevWhite)].x +
        keyGeometry[static_cast<size_t>(prevWhite)].width;
    float x = splitX - (blackWidth * 0.5f);
    x = std::clamp(x, 0.0f, keyboardWidth - blackWidth);
    keyGeometry[static_cast<size_t>(key)] = {x, blackWidth, true};
  }

  const float rowGap = (m_trackCount > 1)
      ? std::max(1.0f, std::min(4.0f, static_cast<float>(viewHeight) * 0.015f))
      : 0.0f;
  const float totalGaps = rowGap * static_cast<float>(m_trackCount - 1);
  const float keyboardHeight =
      (static_cast<float>(viewHeight) - totalGaps) / static_cast<float>(m_trackCount);
  if (keyboardHeight <= 1.0f) {
    return hit;
  }

  const float blackHeight = std::max(2.0f, keyboardHeight * 0.62f);
  const float y = static_cast<float>(pos.y());
  const float x = static_cast<float>(pos.x());

  for (int track = 0; track < m_trackCount; ++track) {
    const float rowY = static_cast<float>(track) * (keyboardHeight + rowGap);
    if (y < rowY || y >= rowY + keyboardHeight) {
      continue;
    }

    const float localY = y - rowY;
    if (localY < blackHeight) {
      for (int key = 0; key < kPianoKeyCount; ++key) {
        if (!isBlackKey(key)) {
          continue;
        }
        const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
        if (!geom.valid) {
          continue;
        }
        if (x >= geom.x && x < geom.x + geom.width) {
          hit.trackIndex = track;
          hit.key = key;
          return hit;
        }
      }
    }

    for (int key = 0; key < kPianoKeyCount; ++key) {
      if (isBlackKey(key)) {
        continue;
      }
      const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
      if (!geom.valid) {
        continue;
      }
      if (x >= geom.x && x < geom.x + geom.width) {
        hit.trackIndex = track;
        hit.key = key;
        return hit;
      }
    }

    return hit;
  }

  return hit;
}

void ActiveNoteView::setPreviewKey(const KeyHitResult& hit) {
  const int targetTrack = hit.valid() ? hit.trackIndex : -1;
  const int targetKey = hit.valid() ? hit.key : -1;
  if (targetTrack == m_previewTrackIndex && targetKey == m_previewKey) {
    return;
  }

  if (m_previewTrackIndex >= 0 &&
      m_previewTrackIndex < static_cast<int>(m_previewKeysByTrack.size()) &&
      m_previewKey >= 0 && m_previewKey < kPianoKeyCount) {
    m_previewKeysByTrack[static_cast<size_t>(m_previewTrackIndex)].reset(
        static_cast<size_t>(m_previewKey));
  }

  m_previewTrackIndex = targetTrack;
  m_previewKey = targetKey;

  if (m_previewTrackIndex >= 0 &&
      m_previewTrackIndex < static_cast<int>(m_previewKeysByTrack.size()) &&
      m_previewKey >= 0 && m_previewKey < kPianoKeyCount) {
    m_previewKeysByTrack[static_cast<size_t>(m_previewTrackIndex)].set(
        static_cast<size_t>(m_previewKey));
    emit notePreviewRequested(m_previewTrackIndex, m_previewKey);
  } else {
    emit notePreviewStopped();
  }

  requestRender();
}

void ActiveNoteView::clearPreviewKey() {
  setPreviewKey(KeyHitResult{});
}

void ActiveNoteView::requestRender() {
  if (m_rhiHost) {
    m_rhiHost->requestUpdate();
  }
}
