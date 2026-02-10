/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollRhiWidget.h"

#include "PianoRollView.h"

#include <rhi/qrhi.h>

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QResizeEvent>
#include <QWheelEvent>

PianoRollRhiWidget::PianoRollRhiWidget(PianoRollView* view, QWidget* parent)
    : QRhiWidget(parent),
      m_view(view),
      m_renderer(view) {
#if defined(Q_OS_LINUX)
  setApi(QRhiWidget::Api::OpenGL);
#elif defined(Q_OS_WIN)
  setApi(QRhiWidget::Api::Direct3D11);
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
  setApi(QRhiWidget::Api::Metal);
#elif QT_CONFIG(opengl)
  setApi(QRhiWidget::Api::OpenGL);
#else
  setApi(QRhiWidget::Api::Null);
#endif

  setMouseTracking(true);
  setAttribute(Qt::WA_AcceptTouchEvents, true);
}

PianoRollRhiWidget::~PianoRollRhiWidget() {
  releaseResources();
}

void PianoRollRhiWidget::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb);
  m_renderer.initIfNeeded(rhi());
}

void PianoRollRhiWidget::render(QRhiCommandBuffer* cb) {
  if (!cb) {
    return;
  }

  QRhi* widgetRhi = rhi();
  if (!widgetRhi) {
    return;
  }

  m_renderer.initIfNeeded(widgetRhi);

  QRhiRenderTarget* rt = renderTarget();
  if (!rt) {
    return;
  }

  PianoRollRhiRenderer::RenderTargetInfo info;
  info.renderTarget = rt;
  info.renderPassDesc = rt->renderPassDescriptor();
  info.pixelSize = rt->pixelSize();
  info.sampleCount = rt->sampleCount();
  info.dpr = rt->devicePixelRatio();
  m_renderer.renderFrame(cb, info);
}

void PianoRollRhiWidget::releaseResources() {
  m_renderer.releaseResources();
}

void PianoRollRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  update();
}

bool PianoRollRhiWidget::event(QEvent* event) {
  if (event && event->type() == QEvent::NativeGesture) {
    if (m_view && m_view->handleViewportNativeGesture(static_cast<QNativeGestureEvent*>(event))) {
      return true;
    }
  }

  return QRhiWidget::event(event);
}

void PianoRollRhiWidget::wheelEvent(QWheelEvent* event) {
  if (m_view && m_view->handleViewportWheel(event)) {
    return;
  }

  event->ignore();
}

void PianoRollRhiWidget::mousePressEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMousePress(event)) {
    return;
  }

  event->ignore();
}

void PianoRollRhiWidget::mouseMoveEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMouseMove(event)) {
    return;
  }

  event->ignore();
}

void PianoRollRhiWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMouseRelease(event)) {
    return;
  }

  event->ignore();
}
