/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiWidget.h"

#include "HexView.h"
#include "HexViewRhiRenderer.h"

#include <rhi/qrhi.h>

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

HexViewRhiWidget::HexViewRhiWidget(HexView* view, QWidget* parent)
    : QRhiWidget(parent),
      m_view(view),
      m_renderer(std::make_unique<HexViewRhiRenderer>(view, "HexViewRhiWidget")) {
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

void HexViewRhiWidget::markBaseDirty() {
  if (m_renderer) {
    m_renderer->markBaseDirty();
  }
}

void HexViewRhiWidget::markSelectionDirty() {
  if (m_renderer) {
    m_renderer->markSelectionDirty();
  }
}

void HexViewRhiWidget::invalidateCache() {
  if (m_renderer) {
    m_renderer->invalidateCache();
  }
}

void HexViewRhiWidget::requestUpdate() {
  update();
}

void HexViewRhiWidget::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb);
  if (m_renderer) {
    m_renderer->initIfNeeded(rhi());
  }
}

void HexViewRhiWidget::render(QRhiCommandBuffer* cb) {
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
  info.dpr = rt->devicePixelRatio();
  m_renderer->renderFrame(cb, info);
}

void HexViewRhiWidget::releaseResources() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }
}

bool HexViewRhiWidget::event(QEvent *e)
{
  if (!m_view || !m_view->viewport())
    return QRhiWidget::event(e);

  switch (e->type()) {
    case QEvent::MouseButtonPress: {
      m_view->setFocus(Qt::MouseFocusReason);
      m_dragging = true;

      // Press/release: keep synchronous so existing HexView logic stays correct.
      QCoreApplication::sendEvent(m_view->viewport(), e);

      requestUpdate();
      return true;
    }

    case QEvent::MouseMove: {
      if (!m_dragging)
        return true;

      // Schedule a frame; Qt will coalesce multiple requestUpdate() calls anyway
      QCoreApplication::sendEvent(m_view->viewport(), e);
      requestUpdate();
      return true;
    }

    case QEvent::MouseButtonRelease: {
      QCoreApplication::sendEvent(m_view->viewport(), e);
      m_dragging = false;
      requestUpdate();
      return true;
    }

    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ToolTip:
      QCoreApplication::sendEvent(m_view->viewport(), e);
      requestUpdate();
      return true;

    default:
      break;
  }      auto *me = static_cast<QMouseEvent*>(e);


  return QRhiWidget::event(e);
}

void HexViewRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  requestUpdate();
}
