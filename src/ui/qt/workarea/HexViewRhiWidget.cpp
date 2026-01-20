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

  // Apply at most one mouse, wheel move per frame
  drainPendingMouseMove();
  drainPendingWheel();

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

void HexViewRhiWidget::drainPendingMouseMove()
{
  if (!m_pendingMouseMove || !m_view || !m_view->viewport())
    return;

  m_pendingMouseMove = false;

  // Map global -> viewport coordinates
  const QPoint vp = m_view->viewport()->mapFromGlobal(m_pendingGlobalPos.toPoint());
  const QPoint vpPos(vp);

  // Update selection/hover/etc once per frame
  m_view->handleCoalescedMouseMove(vpPos, m_pendingButtons, m_pendingMods);
}

void HexViewRhiWidget::drainPendingWheel()
{
  if (!m_pendingWheel || !m_view || !m_view->viewport())
    return;

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
      /*inverted*/ false
  );

  QCoreApplication::sendEvent(m_view->viewport(), &ev);

  m_wheelPixelDelta = {};
  m_wheelAngleDelta = {};

  // Trackpad inertia ends with ScrollEnd; if not available, use a timeout
  if (m_wheelPhase == Qt::ScrollEnd)
    m_scrolling = false;
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
      auto *me = static_cast<QMouseEvent*>(e);
      if (!m_dragging)
        return true;
      // Coalesce: keep only the latest move
      m_pendingMouseMove = true;
      m_pendingGlobalPos = me->globalPosition();
      m_pendingButtons   = me->buttons();
      m_pendingMods      = me->modifiers();

      // Schedule a frame; Qt will coalesce multiple requestUpdate() calls anyway
      requestUpdate();
      return true;
    }

    case QEvent::MouseButtonRelease: {
      QCoreApplication::sendEvent(m_view->viewport(), e);
      m_dragging = false;
      requestUpdate();
      return true;
    }

    case QEvent::Wheel: {
      auto *we = static_cast<QWheelEvent*>(e);
      m_pendingWheel = true;
      m_scrolling = true;

      m_wheelGlobalPos = we->globalPosition();
      m_wheelPixelDelta += we->pixelDelta();
      m_wheelAngleDelta += we->angleDelta();
      m_wheelMods = we->modifiers();
      m_wheelButtons = we->buttons();
      m_wheelPhase = we->phase();

      requestUpdate();
      return true;
    }

    case QEvent::MouseButtonDblClick:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ToolTip:
      QCoreApplication::sendEvent(m_view->viewport(), e);
      requestUpdate();
      return true;

    default:
      break;
  }

  return QRhiWidget::event(e);
}

void HexViewRhiWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  requestUpdate();
}
