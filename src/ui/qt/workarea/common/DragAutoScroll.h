/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QPoint>
#include <QRect>

class QWidget;

namespace QtUi {

struct AutoScrollRampConfig {
  int maxStep = 1;
  int deadZonePixels = 0;
  int rampDistancePixels = 1;
};

struct ScreenEdgeTravelPixels {
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
  bool valid = false;
};

[[nodiscard]] ScreenEdgeTravelPixels screenEdgeTravelPixels(const QWidget* widget,
                                                            const QRect& localRect);

[[nodiscard]] QPoint edgeAutoScrollDelta(const QPoint& pointerPos,
                                         const QRect& boundsRect,
                                         const ScreenEdgeTravelPixels& travel,
                                         const AutoScrollRampConfig& config);

}  // namespace QtUi

