/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteRhiWidget.h"

#include "ActiveNoteView.h"

#include <rhi/qrhi.h>

#include <QResizeEvent>

ActiveNoteRhiWidget::ActiveNoteRhiWidget(ActiveNoteView* view, QWidget* parent)
    : QRhiWidget(parent),
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
}

ActiveNoteRhiWidget::~ActiveNoteRhiWidget() {
  releaseResources();
}

void ActiveNoteRhiWidget::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb);
  m_renderer.initIfNeeded(rhi());
}

void ActiveNoteRhiWidget::render(QRhiCommandBuffer* cb) {
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

  ActiveNoteRhiRenderer::RenderTargetInfo info;
  info.renderTarget = rt;
  info.renderPassDesc = rt->renderPassDescriptor();
  info.pixelSize = rt->pixelSize();
  info.sampleCount = rt->sampleCount();
  info.dpr = rt->devicePixelRatio();
  m_renderer.renderFrame(cb, info);
}

void ActiveNoteRhiWidget::releaseResources() {
  m_renderer.releaseResources();
}

void ActiveNoteRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  update();
}
