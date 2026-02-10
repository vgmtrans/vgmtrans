/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollRhiWindow.h"

#include "PianoRollRhiRenderer.h"
#include "PianoRollView.h"

#include <rhi/qrhi.h>

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>

PianoRollRhiWindow::PianoRollRhiWindow(PianoRollView* view, PianoRollRhiRenderer* renderer)
    : m_view(view),
      m_renderer(renderer) {
}

PianoRollRhiWindow::~PianoRollRhiWindow() {
  releaseRhiResources();
}

bool PianoRollRhiWindow::handleWindowEvent(QEvent* e) {
  if (!e || !m_view) {
    return false;
  }

  // Forward all user interaction to PianoRollView so both the window-backed
  // and widget-backed surfaces share identical behavior.
  switch (e->type()) {
    case QEvent::NativeGesture:
      return m_view->handleViewportNativeGesture(static_cast<QNativeGestureEvent*>(e));
    case QEvent::Wheel: {
      auto* wheel = static_cast<QWheelEvent*>(e);
      if (m_view->handleViewportWheel(wheel)) {
        return true;
      }

      // Plain scrolling path (no zoom modifiers). This mirrors scroll-area
      // semantics while staying in the RHI window input path.
      QScrollBar* hbar = m_view->horizontalScrollBar();
      QScrollBar* vbar = m_view->verticalScrollBar();
      if (!hbar || !vbar) {
        return false;
      }

      int dx = 0;
      int dy = 0;
      const QPoint pixelDelta = wheel->pixelDelta();
      if (!pixelDelta.isNull()) {
        dx = pixelDelta.x();
        dy = pixelDelta.y();
      } else {
        const QPoint angleDelta = wheel->angleDelta();
        const int hStep = std::max(1, hbar->singleStep());
        const int vStep = std::max(1, vbar->singleStep());
        dx = (angleDelta.x() * hStep) / 120;
        dy = (angleDelta.y() * vStep) / 120;
        if (dx == 0 && angleDelta.x() != 0) {
          dx = (angleDelta.x() > 0) ? hStep : -hStep;
        }
        if (dy == 0 && angleDelta.y() != 0) {
          dy = (angleDelta.y() > 0) ? vStep : -vStep;
        }
      }

      if (wheel->modifiers().testFlag(Qt::ShiftModifier) && dx == 0 && dy != 0) {
        // Shift+wheel convention: vertical wheel drives horizontal scroll.
        dx = dy;
        dy = 0;
      }

      if (dx == 0 && dy == 0) {
        return false;
      }

      if (dx != 0) {
        hbar->setValue(std::clamp(hbar->value() - dx, hbar->minimum(), hbar->maximum()));
      }
      if (dy != 0) {
        vbar->setValue(std::clamp(vbar->value() - dy, vbar->minimum(), vbar->maximum()));
      }
      wheel->accept();
      return true;
    }
    case QEvent::MouseButtonPress:
      return m_view->handleViewportMousePress(static_cast<QMouseEvent*>(e));
    case QEvent::MouseMove:
      return m_view->handleViewportMouseMove(static_cast<QMouseEvent*>(e));
    case QEvent::MouseButtonRelease:
      return m_view->handleViewportMouseRelease(static_cast<QMouseEvent*>(e));
    default:
      break;
  }

  return false;
}

void PianoRollRhiWindow::onRhiInitialized(QRhi* rhi) {
  if (m_renderer) {
    m_renderer->initIfNeeded(rhi);
  }
}

void PianoRollRhiWindow::onRhiReleased() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }
}

void PianoRollRhiWindow::renderRhiFrame(QRhiCommandBuffer* cb,
                                        QRhiRenderTarget* renderTarget,
                                        QRhiRenderPassDescriptor* renderPassDesc,
                                        const QSize& pixelSize,
                                        int sampleCount,
                                        float dpr) {
  if (!m_view || !m_view->viewport() || !m_renderer) {
    return;
  }

  // Repackage swapchain/window target details into renderer-neutral frame info.
  PianoRollRhiRenderer::RenderTargetInfo info;
  info.renderTarget = renderTarget;
  info.renderPassDesc = renderPassDesc;
  info.pixelSize = pixelSize;
  info.sampleCount = sampleCount;
  info.dpr = dpr;
  m_renderer->renderFrame(cb, info);
}
