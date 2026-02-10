/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteView.h"

#include "ActiveNoteRhiHost.h"

#include <QColor>
#include <QPalette>
#include <QResizeEvent>

#include <algorithm>

ActiveNoteView::ActiveNoteView(QWidget* parent)
    : QAbstractScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setFocusPolicy(Qt::NoFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_rhiHost = new ActiveNoteRhiHost(this, viewport());
  m_rhiHost->setGeometry(viewport()->rect());
  m_rhiHost->setFocusPolicy(Qt::NoFocus);
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

  m_activeKeysByTrack.assign(static_cast<size_t>(m_trackCount), {});
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

void ActiveNoteView::requestRender() {
  if (m_rhiHost) {
    m_rhiHost->requestUpdate();
  }
}
