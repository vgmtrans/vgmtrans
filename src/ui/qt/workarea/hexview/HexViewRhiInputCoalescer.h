/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QPoint>
#include <QWheelEvent>
#include "HexView.h"

class HexViewRhiInputCoalescer final {
public:
  explicit HexViewRhiInputCoalescer(HexView* view = nullptr)
      : m_view(view) {}

  void setView(HexView* view) { m_view = view; }

  void queueMouseMove(const QMouseEvent* event) {
    if (!event) {
      return;
    }
    m_pendingMouseMove = true;
    m_pendingGlobalPos = event->globalPosition();
    m_pendingButtons = event->buttons();
    m_pendingMods = event->modifiers();
  }

  void queueWheel(const QWheelEvent* event) {
    if (!event) {
      return;
    }
    m_pendingWheel = true;

    QPoint pixel = event->pixelDelta();
    QPoint angle = event->angleDelta();
    applyAltWheelFix(event->modifiers(), pixel, angle);

    m_wheelGlobalPos = event->globalPosition();
    m_wheelPixelDelta += pixel;
    m_wheelAngleDelta += angle;
    m_wheelMods = event->modifiers();
    m_wheelButtons = event->buttons();
    m_wheelPhase = event->phase();
  }

  void drainPendingMouseMove() {
    if (!m_pendingMouseMove || !m_view || !m_view->viewport()) {
      return;
    }
    m_pendingMouseMove = false;
    const QPoint vp = m_view->viewport()->mapFromGlobal(m_pendingGlobalPos.toPoint());
    m_view->handleCoalescedMouseMove(vp, m_pendingButtons, m_pendingMods);
  }

  void drainPendingWheel() {
    if (!m_pendingWheel || !m_view || !m_view->viewport()) {
      return;
    }
    m_pendingWheel = false;

    const QPoint vp = m_view->viewport()->mapFromGlobal(m_wheelGlobalPos.toPoint());
    const QPointF vpPos(vp);

    QWheelEvent ev(
        vpPos,
        m_wheelGlobalPos,
        m_wheelPixelDelta,
        m_wheelAngleDelta,
        m_wheelButtons,
        m_wheelMods,
        m_wheelPhase,
        /*inverted*/ false);

    QCoreApplication::sendEvent(m_view->viewport(), &ev);

    m_wheelPixelDelta = {};
    m_wheelAngleDelta = {};
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

  HexView* m_view = nullptr;

  bool m_pendingMouseMove = false;
  QPointF m_pendingGlobalPos;
  Qt::MouseButtons m_pendingButtons = Qt::NoButton;
  Qt::KeyboardModifiers m_pendingMods = Qt::NoModifier;

  bool m_pendingWheel = false;
  QPointF m_wheelGlobalPos;
  QPoint m_wheelPixelDelta;
  QPoint m_wheelAngleDelta;
  Qt::KeyboardModifiers m_wheelMods = Qt::NoModifier;
  Qt::MouseButtons m_wheelButtons = Qt::NoButton;
  Qt::ScrollPhase m_wheelPhase = Qt::NoScrollPhase;
};
