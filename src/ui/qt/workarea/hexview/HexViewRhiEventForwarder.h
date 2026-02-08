/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <functional>

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "HexView.h"
#include "HexViewRhiInputCoalescer.h"

class HexViewRhiEventForwarder final {
public:
  using UpdateFn = std::function<void()>;

  explicit HexViewRhiEventForwarder(HexView* view = nullptr, UpdateFn updateFn = {})
      : m_view(view), m_input(view), m_requestUpdate(std::move(updateFn)) {}

  void drainPendingInput() {
    m_input.drainPendingMouseMove();
    m_input.drainPendingWheel();
  }

  bool handleEvent(QEvent* event, bool& dragging) {
    if (!event || !m_view || !m_view->viewport()) {
      return false;
    }

    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        m_view->setFocus(Qt::MouseFocusReason);
        dragging = true;
        QCoreApplication::sendEvent(m_view->viewport(), event);
        requestUpdate();
        return true;
      }

      case QEvent::MouseButtonDblClick: {
        // Ensure the second click still behaves like a press for quick select/deselect.
        auto* me = static_cast<QMouseEvent*>(event);
        if (!dragging && me->button() == Qt::LeftButton) {
          QMouseEvent pressEvent(QEvent::MouseButtonPress,
                                 me->position(),
                                 me->globalPosition(),
                                 me->button(),
                                 me->buttons(),
                                 me->modifiers());
          m_view->setFocus(Qt::MouseFocusReason);
          dragging = true;
          QCoreApplication::sendEvent(m_view->viewport(), &pressEvent);
        }
        QCoreApplication::sendEvent(m_view->viewport(), event);
        requestUpdate();
        return true;
      }

      case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!dragging) {
          const QPoint vp = m_view->viewport()->mapFromGlobal(me->globalPosition().toPoint());
          m_view->handleTooltipHoverMove(vp, me->modifiers());
          return true;
        }
        m_input.queueMouseMove(me);
        requestUpdate();
        return true;
      }

      case QEvent::MouseButtonRelease:
        QCoreApplication::sendEvent(m_view->viewport(), event);
        dragging = false;
        requestUpdate();
        return true;

      case QEvent::Wheel:
        m_input.queueWheel(static_cast<QWheelEvent*>(event));
        requestUpdate();
        return true;

      case QEvent::KeyPress:
      case QEvent::KeyRelease:
      case QEvent::ToolTip:
        QCoreApplication::sendEvent(m_view->viewport(), event);
        requestUpdate();
        return true;

      default:
        return false;
    }
  }

private:
  void requestUpdate() const {
    if (m_requestUpdate) {
      m_requestUpdate();
    }
  }

  HexView* m_view = nullptr;
  HexViewRhiInputCoalescer m_input;
  UpdateFn m_requestUpdate;
};
