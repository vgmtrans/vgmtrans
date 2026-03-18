/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>

class PianoRollGeometry {
public:
  struct Metrics {
    int viewportWidth = 0;
    int viewportHeight = 0;
    int scrollX = 0;
    int scrollY = 0;
    int keyboardWidth = 0;
    int topBarHeight = 0;
    int midiKeyCount = 0;
    int totalTicks = 1;
    float pixelsPerTick = 1.0f;
    float pixelsPerKey = 1.0f;
  };

  explicit PianoRollGeometry(Metrics metrics);

  [[nodiscard]] int noteViewportWidth() const;
  [[nodiscard]] int noteViewportHeight() const;
  [[nodiscard]] int contentWidth() const;
  [[nodiscard]] int contentHeight() const;
  [[nodiscard]] int clampTick(int tick) const;
  [[nodiscard]] int scanlineWorldX(int tick) const;
  [[nodiscard]] int scanlinePixelX(int tick) const;
  [[nodiscard]] bool isTickVisible(int tick) const;
  [[nodiscard]] int tickFromViewportX(int x) const;
  [[nodiscard]] QRect graphRectInViewport() const;
  [[nodiscard]] QPoint graphWorldPosFromViewport(const QPoint& viewportPos) const;
  [[nodiscard]] QPoint graphViewportPosFromWorld(const QPoint& worldPos) const;
  [[nodiscard]] QRectF graphSelectionRectInViewport(const QPoint& anchorViewport,
                                                    const QPoint& currentViewport,
                                                    bool anchorWorldValid,
                                                    const QPoint& anchorWorld) const;

private:
  Metrics m_metrics;
};
