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
#include <QWheelEvent>

PianoRollRhiWindow::PianoRollRhiWindow(PianoRollView* view, PianoRollRhiRenderer* renderer)
    : m_view(view),
      m_renderer(renderer) {
}

bool PianoRollRhiWindow::handleWindowEvent(QEvent* e) {
  if (!e || !m_view) {
    return false;
  }

  switch (e->type()) {
    case QEvent::NativeGesture:
      return m_view->handleViewportNativeGesture(static_cast<QNativeGestureEvent*>(e));
    case QEvent::Wheel:
      return m_view->handleViewportWheel(static_cast<QWheelEvent*>(e));
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

  PianoRollRhiRenderer::RenderTargetInfo info;
  info.renderTarget = renderTarget;
  info.renderPassDesc = renderPassDesc;
  info.pixelSize = pixelSize;
  info.sampleCount = sampleCount;
  info.dpr = dpr;
  m_renderer->renderFrame(cb, info);
}
