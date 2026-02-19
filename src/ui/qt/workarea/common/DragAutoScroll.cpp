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
#include <utility>

namespace QtUi {

namespace {

std::pair<int, int> tunedDeadZoneAndRamp(int travelPixels,
                                         int baseDeadZonePixels,
                                         int baseRampDistancePixels) {
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

int axisAutoScrollStep(int value,
                       int minValue,
                       int maxValue,
                       int travelToMinPixels,
                       int travelToMaxPixels,
                       const AutoScrollRampConfig& config) {
  if (config.maxStep <= 0) {
    return 0;
  }

  int distance = 0;
  int direction = 0;
  if (value < minValue) {
    distance = minValue - value;
    direction = 1;
  } else if (value > maxValue) {
    distance = value - maxValue;
    direction = -1;
  } else {
    return 0;
  }

  const int baseTotal = std::max(1, config.deadZonePixels + config.rampDistancePixels);
  const int rawTravelPixels = (direction > 0) ? travelToMinPixels : travelToMaxPixels;
  const int travelPixels = (rawTravelPixels > 0) ? rawTravelPixels : baseTotal;
  const auto [deadZonePixels, rampDistancePixels] =
      tunedDeadZoneAndRamp(travelPixels, config.deadZonePixels, config.rampDistancePixels);
  if (distance <= deadZonePixels) {
    return 0;
  }

  const int effectiveDistance = distance - deadZonePixels;
  if (rampDistancePixels <= 1) {
    return direction * config.maxStep;
  }

  const int rampedDistance = std::min(effectiveDistance, rampDistancePixels);
  const int step = 1 + ((rampedDistance - 1) * (config.maxStep - 1)) / (rampDistancePixels - 1);
  if (step <= 0) {
    return 0;
  }

  return direction * step;
}

}  // namespace

ScreenEdgeTravelPixels screenEdgeTravelPixels(const QWidget* widget, const QRect& localRect) {
  ScreenEdgeTravelPixels travel;
  if (!widget || localRect.isEmpty()) {
    return travel;
  }

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

QPoint edgeAutoScrollDelta(const QPoint& pointerPos,
                           const QRect& boundsRect,
                           const ScreenEdgeTravelPixels& travel,
                           const AutoScrollRampConfig& config) {
  if (boundsRect.isEmpty()) {
    return {};
  }

  const int leftTravel = travel.valid ? travel.left : 0;
  const int rightTravel = travel.valid ? travel.right : 0;
  const int topTravel = travel.valid ? travel.top : 0;
  const int bottomTravel = travel.valid ? travel.bottom : 0;

  return QPoint(axisAutoScrollStep(pointerPos.x(),
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

