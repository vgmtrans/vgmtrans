/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollGeometry.h"

#include <algorithm>
#include <cmath>

PianoRollGeometry::PianoRollGeometry(Metrics metrics)
    : m_metrics(metrics) {
}

int PianoRollGeometry::noteViewportWidth() const {
  return std::max(0, m_metrics.viewportWidth - m_metrics.keyboardWidth);
}

int PianoRollGeometry::noteViewportHeight() const {
  return std::max(0, m_metrics.viewportHeight - m_metrics.topBarHeight);
}

int PianoRollGeometry::contentWidth() const {
  return std::max(
      1,
      static_cast<int>(std::ceil(static_cast<float>(std::max(1, m_metrics.totalTicks)) * m_metrics.pixelsPerTick)));
}

int PianoRollGeometry::contentHeight() const {
  return std::max(
      1,
      static_cast<int>(std::ceil(static_cast<float>(std::max(0, m_metrics.midiKeyCount)) * m_metrics.pixelsPerKey)));
}

int PianoRollGeometry::clampTick(int tick) const {
  return std::clamp(tick, 0, std::max(1, m_metrics.totalTicks));
}

int PianoRollGeometry::scanlineWorldX(int tick) const {
  return static_cast<int>(std::lround(
      static_cast<float>(clampTick(tick)) * std::max(0.0001f, m_metrics.pixelsPerTick)));
}

int PianoRollGeometry::scanlinePixelX(int tick) const {
  return scanlineWorldX(tick) - m_metrics.scrollX;
}

bool PianoRollGeometry::isTickVisible(int tick) const {
  const int width = noteViewportWidth();
  if (width <= 0) {
    return true;
  }

  const int scanX = scanlinePixelX(tick);
  return scanX >= 0 && scanX < width;
}

int PianoRollGeometry::tickFromViewportX(int x) const {
  if (m_metrics.pixelsPerTick <= 0.0f) {
    return 0;
  }

  const int localX = std::max(0, x - m_metrics.keyboardWidth);
  const float absoluteX = static_cast<float>(m_metrics.scrollX + localX);
  const int tick = static_cast<int>(std::llround(absoluteX / m_metrics.pixelsPerTick));
  return clampTick(tick);
}

QRect PianoRollGeometry::graphRectInViewport() const {
  return QRect(m_metrics.keyboardWidth,
               m_metrics.topBarHeight,
               noteViewportWidth(),
               noteViewportHeight());
}

QPoint PianoRollGeometry::graphWorldPosFromViewport(const QPoint& viewportPos) const {
  return QPoint(m_metrics.scrollX + (viewportPos.x() - m_metrics.keyboardWidth),
                m_metrics.scrollY + (viewportPos.y() - m_metrics.topBarHeight));
}

QPoint PianoRollGeometry::graphViewportPosFromWorld(const QPoint& worldPos) const {
  return QPoint(m_metrics.keyboardWidth + (worldPos.x() - m_metrics.scrollX),
                m_metrics.topBarHeight + (worldPos.y() - m_metrics.scrollY));
}

QRectF PianoRollGeometry::graphSelectionRectInViewport(const QPoint& anchorViewport,
                                                       const QPoint& currentViewport,
                                                       bool anchorWorldValid,
                                                       const QPoint& anchorWorld) const {
  const QRect graphRect = graphRectInViewport();
  if (graphRect.isEmpty()) {
    return {};
  }

  const QPoint marqueeAnchorViewport = anchorWorldValid
                                           ? graphViewportPosFromWorld(anchorWorld)
                                           : anchorViewport;
  const float anchorX = static_cast<float>(marqueeAnchorViewport.x());
  const float anchorY = static_cast<float>(marqueeAnchorViewport.y());
  const float currentX = static_cast<float>(currentViewport.x());
  const float currentY = static_cast<float>(currentViewport.y());

  const float left = std::min(anchorX, currentX);
  const float top = std::min(anchorY, currentY);
  const float right = std::max(anchorX, currentX);
  const float bottom = std::max(anchorY, currentY);
  return QRectF(left, top, right - left, bottom - top);
}
