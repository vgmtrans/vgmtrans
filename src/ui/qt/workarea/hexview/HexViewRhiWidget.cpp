/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiWidget.h"

#include "HexViewRhiRenderer.h"

#include <rhi/qrhi.h>

#include <QEvent>
#include <QResizeEvent>

HexViewRhiWidget::HexViewRhiWidget(HexView* view, HexViewRhiRenderer* renderer,
                                   QWidget* parent)
    : QRhiWidget(parent),
      m_view(view),
      m_renderer(renderer),
      m_eventForwarder(view, [this]() { update(); }) {
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

HexViewRhiWidget::~HexViewRhiWidget() {
  releaseResources();
}

void HexViewRhiWidget::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb);
  if (m_renderer) {
    m_renderer->initIfNeeded(rhi());
  }
}

void HexViewRhiWidget::render(QRhiCommandBuffer* cb) {
  m_eventForwarder.drainPendingInput();

  if (!cb || !m_renderer) {
    return;
  }

  QRhi* widgetRhi = rhi();
  if (!widgetRhi) {
    return;
  }
  m_renderer->initIfNeeded(widgetRhi);

  QRhiRenderTarget* rt = renderTarget();
  if (!rt) {
    return;
  }

  HexViewRhiRenderer::RenderTargetInfo info;
  info.renderTarget = rt;
  info.renderPassDesc = rt->renderPassDescriptor();
  info.pixelSize = rt->pixelSize();
  info.sampleCount = rt->sampleCount();
  // Use the widget's DPR as reported by the windowing system. This is important on some hi-dpi systems (notably Retina screens)
  info.dpr = devicePixelRatio();
  m_renderer->renderFrame(cb, info);
}

void HexViewRhiWidget::releaseResources() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }
}

bool HexViewRhiWidget::event(QEvent* e) {
  if (m_eventForwarder.handleEvent(e, m_dragging)) {
    return true;
  }
  return QRhiWidget::event(e);
}

void HexViewRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  update();
}
