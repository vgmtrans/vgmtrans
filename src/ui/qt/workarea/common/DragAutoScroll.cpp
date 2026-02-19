/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "DragAutoScroll.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace QtUi {

namespace {

// Scales the dead zone and ramp to fit available cursor travel.
std::pair<int, int> tunedDeadZoneAndRamp(int travelPixels,
                                         int baseDeadZonePixels,
                                         int baseRampDistancePixels) {
  // Compress dead-zone + ramp into available cursor travel so max speed is still reachable
  // even when the window is flush against a screen edge.
  const int baseTotal = std::max(1, baseDeadZonePixels + baseRampDistancePixels);
  if (travelPixels <= 0) {
    return {0, 1};
  }

  const int total = std::clamp(travelPixels, 1, baseTotal);
  int deadZonePixels = (baseDeadZonePixels * total) / baseTotal;
  deadZonePixels = std::clamp(deadZonePixels, 0, std::max(0, total - 1));
  const int rampDistancePixels = std::max(1, total - deadZonePixels);
  return {deadZonePixels, rampDistancePixels};
}

// Computes one-axis fractional auto-scroll step from pointer overflow and ramp config.
float axisAutoScrollStep(int value,
                         int minValue,
                         int maxValue,
                         int travelToMinPixels,
                         int travelToMaxPixels,
                         const AutoScrollRampConfig& config) {
  if (config.maxStep <= 0) {
    return 0.0f;
  }

  int distance = 0;
  int direction = 0;
  if (value < minValue) {
    distance = minValue - value;
    direction = 1;   // outside min edge: positive pan delta
  } else if (value > maxValue) {
    distance = value - maxValue;
    direction = -1;  // outside max edge: negative pan delta
  } else {
    return 0.0f;
  }

  const int baseTotal = std::max(1, config.deadZonePixels + config.rampDistancePixels);
  const int rawTravelPixels = (direction > 0) ? travelToMinPixels : travelToMaxPixels;
  // If monitor travel is unavailable, fall back to the base profile.
  const int travelPixels = (rawTravelPixels > 0) ? rawTravelPixels : baseTotal;
  const auto [deadZonePixels, rampDistancePixels] =
      tunedDeadZoneAndRamp(travelPixels, config.deadZonePixels, config.rampDistancePixels);
  if (distance <= deadZonePixels) {
    return 0.0f;
  }

  const int effectiveDistance = distance - deadZonePixels;
  if (rampDistancePixels <= 1) {
    return static_cast<float>(direction * config.maxStep);
  }

  const int rampedDistance = std::min(effectiveDistance, rampDistancePixels);
  const float normalized = std::clamp(static_cast<float>(rampedDistance) /
                                          static_cast<float>(rampDistancePixels),
                                      0.0f,
                                      1.0f);
  // Quadratic easing gives finer low-speed control just past the dead zone.
  const float step = static_cast<float>(config.maxStep) * std::pow(normalized, 2.0f);

  return static_cast<float>(direction) * step;
}

}  // namespace

// Measures how many pixels the cursor can move from each edge to the screen bounds.
ScreenEdgeTravelPixels screenEdgeTravelPixels(const QWidget* widget, const QRect& localRect) {
  ScreenEdgeTravelPixels travel;
  if (!widget || localRect.isEmpty()) {
    return travel;
  }

  // Pick the screen that contains the drag region center, then measure available cursor
  // travel from each edge of that region to the screen bounds.
  const QPoint centerGlobal = widget->mapToGlobal(localRect.center());
  QScreen* screen = QGuiApplication::screenAt(centerGlobal);
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    return travel;
  }

  const QRect screenGeometry = screen->geometry();
  const QPoint topLeftGlobal = widget->mapToGlobal(localRect.topLeft());
  const QPoint bottomRightGlobal = widget->mapToGlobal(localRect.bottomRight());

  travel.left = std::max(0, topLeftGlobal.x() - screenGeometry.left());
  travel.right = std::max(0, screenGeometry.right() - bottomRightGlobal.x());
  travel.top = std::max(0, topLeftGlobal.y() - screenGeometry.top());
  travel.bottom = std::max(0, screenGeometry.bottom() - bottomRightGlobal.y());
  travel.valid = true;
  return travel;
}

// Computes two-axis fractional auto-scroll delta for a pointer relative to drag bounds.
QPointF edgeAutoScrollDelta(const QPoint& pointerPos,
                            const QRect& boundsRect,
                            const ScreenEdgeTravelPixels& travel,
                            const AutoScrollRampConfig& config) {
  if (boundsRect.isEmpty()) {
    return {};
  }

  // Invalid travel means "unknown screen context"; axisAutoScrollStep will use base tuning.
  const int leftTravel = travel.valid ? travel.left : 0;
  const int rightTravel = travel.valid ? travel.right : 0;
  const int topTravel = travel.valid ? travel.top : 0;
  const int bottomTravel = travel.valid ? travel.bottom : 0;

  return QPointF(axisAutoScrollStep(pointerPos.x(),
                                    boundsRect.left(),
                                    boundsRect.right(),
                                    leftTravel,
                                    rightTravel,
                                    config),
                 axisAutoScrollStep(pointerPos.y(),
                                    boundsRect.top(),
                                    boundsRect.bottom(),
                                    topTravel,
                                    bottomTravel,
                                    config));
}

}  // namespace QtUi
