/*
* VGMTrans (c) 2002-2026
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

HexViewRhiWidget::HexViewRhiWidget(HexView* view, HexViewRhiRenderer* renderer,
                                   QWidget* parent)
    : QRhiWidget(parent), m_view(view), m_renderer(renderer) {
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

      update();
      return true;
    }

    case QEvent::MouseButtonDblClick: {
      // the second of two quick clicks will be coalesced into a double click event,
      // we want the second click to register as a normal MouseButtonPress to allow
      // fast select / deselect
      auto *me = static_cast<QMouseEvent*>(e);
      if (!m_dragging && me->button() == Qt::LeftButton) {
        QMouseEvent pressEvent(QEvent::MouseButtonPress,
                               me->position(),
                               me->globalPosition(),
                               me->button(),
                               me->buttons(),
                               me->modifiers());
        m_view->setFocus(Qt::MouseFocusReason);
        m_dragging = true;
        QCoreApplication::sendEvent(m_view->viewport(), &pressEvent);
      }
      QCoreApplication::sendEvent(m_view->viewport(), e);
      update();
      return true;
    }

    case QEvent::MouseMove: {
      if (!m_dragging)
        return true;

      QCoreApplication::sendEvent(m_view->viewport(), e);
      update();
      return true;
    }

    case QEvent::MouseButtonRelease: {
      QCoreApplication::sendEvent(m_view->viewport(), e);
      m_dragging = false;
      update();
      return true;
    }

    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ToolTip:
      QCoreApplication::sendEvent(m_view->viewport(), e);
      update();
      return true;

    default:
      break;
  }

  return QRhiWidget::event(e);
}

void HexViewRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  update();
}
