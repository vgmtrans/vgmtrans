/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteRhiWidget.h"

#include "ActiveNoteView.h"

#include <rhi/qrhi.h>

#include <QMouseEvent>
#include <QResizeEvent>

ActiveNoteRhiWidget::ActiveNoteRhiWidget(ActiveNoteView* view,
                                         ActiveNoteRhiRenderer* renderer,
                                         QWidget* parent)
    : QRhiWidget(parent),
      m_view(view),
      m_renderer(renderer) {
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
}

ActiveNoteRhiWidget::~ActiveNoteRhiWidget() {
  releaseResources();
}

void ActiveNoteRhiWidget::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb);
  if (m_renderer) {
    m_renderer->initIfNeeded(rhi());
  }
}

void ActiveNoteRhiWidget::render(QRhiCommandBuffer* cb) {
  if (!cb) {
    return;
  }

  QRhi* widgetRhi = rhi();
  if (!widgetRhi) {
    return;
  }

  if (!m_renderer) {
    return;
  }
  m_renderer->initIfNeeded(widgetRhi);

  QRhiRenderTarget* rt = renderTarget();
  if (!rt) {
    return;
  }

  ActiveNoteRhiRenderer::RenderTargetInfo info;
  info.renderTarget = rt;
  info.renderPassDesc = rt->renderPassDescriptor();
  info.pixelSize = rt->pixelSize();
  info.sampleCount = rt->sampleCount();
  info.dpr = rt->devicePixelRatio();
  m_renderer->renderFrame(cb, info);
}

void ActiveNoteRhiWidget::releaseResources() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }
}

void ActiveNoteRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  update();
}

void ActiveNoteRhiWidget::mousePressEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMousePress(event)) {
    return;
  }

  event->ignore();
}

void ActiveNoteRhiWidget::mouseMoveEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMouseMove(event)) {
    return;
  }

  event->ignore();
}

void ActiveNoteRhiWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (m_view && m_view->handleViewportMouseRelease(event)) {
    return;
  }

  event->ignore();
}
