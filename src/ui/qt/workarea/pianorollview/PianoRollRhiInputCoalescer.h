/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QNativeGestureEvent>
#include <QPoint>
#include <QWheelEvent>

class PianoRollRhiInputCoalescer final {
public:
  struct WheelBatch {
    QPointF globalPos;
    QPoint pixelDelta;
    QPoint angleDelta;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::ScrollPhase phase = Qt::NoScrollPhase;
    bool inverted = false;
  };

  struct ZoomBatch {
    QPointF globalPos;
    float rawDelta = 0.0f;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
  };

  void queueWheel(const QWheelEvent* event) {
    if (!event) {
      return;
    }

    m_pendingWheel = true;

    QPoint pixel = event->pixelDelta();
    QPoint angle = event->angleDelta();
    applyAltWheelFix(event->modifiers(), pixel, angle);

    m_wheel.globalPos = event->globalPosition();
    m_wheel.pixelDelta += pixel;
    m_wheel.angleDelta += angle;
    m_wheel.modifiers = event->modifiers();
    m_wheel.buttons = event->buttons();
    m_wheel.phase = event->phase();
    m_wheel.inverted = event->inverted();
  }

  bool takePendingWheel(WheelBatch& out) {
    if (!m_pendingWheel) {
      return false;
    }

    m_pendingWheel = false;
    out = m_wheel;
    m_wheel.pixelDelta = {};
    m_wheel.angleDelta = {};
    return true;
  }

  void queueNativeZoomGesture(const QNativeGestureEvent* event) {
    if (!event || event->gestureType() != Qt::ZoomNativeGesture) {
      return;
    }

    m_pendingZoomGesture = true;
    m_zoom.globalPos = event->globalPosition();
    m_zoom.rawDelta += static_cast<float>(event->value());
    m_zoom.modifiers = event->modifiers();
  }

  bool takePendingZoomGesture(ZoomBatch& out) {
    if (!m_pendingZoomGesture) {
      return false;
    }

    m_pendingZoomGesture = false;
    out = m_zoom;
    m_zoom.rawDelta = 0.0f;
    return true;
  }

private:
  static void applyAltWheelFix(Qt::KeyboardModifiers mods, QPoint& pixel, QPoint& angle) {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // Some platforms map Alt+wheel vertical scroll to the horizontal axis.
    if ((mods & Qt::AltModifier) && angle.y() == 0 && angle.x() != 0) {
      angle = QPoint(0, angle.x());
      if (pixel.y() == 0 && pixel.x() != 0) {
        pixel = QPoint(0, pixel.x());
      }
    }
#else
    Q_UNUSED(mods);
    Q_UNUSED(pixel);
    Q_UNUSED(angle);
#endif
  }

  bool m_pendingWheel = false;
  WheelBatch m_wheel;
  bool m_pendingZoomGesture = false;
  ZoomBatch m_zoom;
};
