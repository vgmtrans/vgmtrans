/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiWindow.h"

#include "HexView.h"
#include "HexViewRhiRenderer.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QExposeEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#endif

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

namespace {
bool isRhiDebugEnabled() {
  // return qEnvironmentVariableIsSet("VGMTRANS_HEXVIEW_RHI_DEBUG");
  return true;
}
}  // namespace

struct HexViewRhiWindow::BackendData {
  QRhi::Implementation backend = QRhi::Null;
  QRhiInitParams* initParams = nullptr;

#if QT_CONFIG(opengl)
  QRhiGles2InitParams glesParams;
  std::unique_ptr<QOffscreenSurface> fallbackSurface;
#endif

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
  QRhiVulkanInitParams vkParams;
  std::unique_ptr<QVulkanInstance> vkInstance;
#endif

#if QT_CONFIG(metal)
  QRhiMetalInitParams metalParams;
#endif

#if defined(Q_OS_WIN)
  QRhiD3D11InitParams d3d11Params;
#endif

  QRhiNullInitParams nullParams;
};

HexViewRhiWindow::HexViewRhiWindow(HexView* view, HexViewRhiRenderer* renderer)
    : m_view(view), m_renderer(renderer), m_backend(std::make_unique<BackendData>()) {
  setFlags(Qt::FramelessWindowHint);

#if defined(Q_OS_WIN)
  setSurfaceType(QSurface::Direct3DSurface);
  m_backend->backend = QRhi::D3D11;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
  setSurfaceType(QSurface::MetalSurface);
  m_backend->backend = QRhi::Metal;
#elif QT_CONFIG(vulkan)
  setSurfaceType(QSurface::VulkanSurface);
  m_backend->backend = QRhi::Vulkan;
#elif QT_CONFIG(opengl)
  setSurfaceType(QSurface::OpenGLSurface);
  m_backend->backend = QRhi::OpenGLES2;
#else
  setSurfaceType(QSurface::RasterSurface);
  m_backend->backend = QRhi::Null;
#endif

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
  if (m_backend->backend == QRhi::Vulkan) {
    m_backend->vkInstance = std::make_unique<QVulkanInstance>();
    m_backend->vkInstance->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    if (!m_backend->vkInstance->create()) {
      qWarning("Failed to create QVulkanInstance for HexViewRhiWindow");
      m_backend->vkInstance.reset();
      setSurfaceType(QSurface::RasterSurface);
      m_backend->backend = QRhi::Null;
    } else {
      setVulkanInstance(m_backend->vkInstance.get());
    }
  }
#endif
}

HexViewRhiWindow::~HexViewRhiWindow() {
  releaseResources();
}

void HexViewRhiWindow::drainPendingMouseMove()
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

void HexViewRhiWindow::drainPendingWheel()
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

void HexViewRhiWindow::releaseSwapChain()
{
  if (m_hasSwapChain && m_sc) {
    m_hasSwapChain = false;
    m_sc->destroy();
  }
}

bool HexViewRhiWindow::event(QEvent *e)
{
  if (e->type() == QEvent::UpdateRequest) {
    renderFrame();
    return true;
  }

  if (!m_view || !m_view->viewport())
    return QWindow::event(e);

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
      if (!m_dragging) {
        const QPoint vp = m_view->viewport()->mapFromGlobal(me->globalPosition().toPoint());
        m_view->handleAltHoverMove(vp, me->modifiers());
        return true;
      }
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

    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop: {
      if (auto* target = m_view ? m_view->window() : nullptr) {
        QCoreApplication::sendEvent(target, e);
      }
      e->accept();
      return true;
    }

    default:
      break;
  }

  return QWindow::event(e);
}

void HexViewRhiWindow::exposeEvent(QExposeEvent*) {
  if (!isExposed()) {
    return;
  }
  initIfNeeded();
  requestUpdate();
}

void HexViewRhiWindow::resizeEvent(QResizeEvent*) {
  if (m_rhi) {
    resizeSwapChain();
  }
  requestUpdate();
}

void HexViewRhiWindow::initIfNeeded() {
  if (m_rhi || !m_backend) {
    return;
  }

  const bool debug = debugLoggingEnabled();
  switch (m_backend->backend) {
    case QRhi::OpenGLES2:
#if QT_CONFIG(opengl)
      m_backend->glesParams.window = this;
      m_backend->glesParams.format = QSurfaceFormat::defaultFormat();
      m_backend->fallbackSurface.reset(
          QRhiGles2InitParams::newFallbackSurface(m_backend->glesParams.format));
      m_backend->glesParams.fallbackSurface = m_backend->fallbackSurface.get();
      m_backend->initParams = &m_backend->glesParams;
#else
      m_backend->backend = QRhi::Null;
      m_backend->initParams = &m_backend->nullParams;
#endif
      break;
    case QRhi::Vulkan:
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
      if (!m_backend->vkInstance) {
        qWarning("Vulkan backend requested but QVulkanInstance is missing");
        m_backend->backend = QRhi::Null;
        m_backend->initParams = &m_backend->nullParams;
        break;
      }
      m_backend->vkParams.inst = m_backend->vkInstance.get();
      m_backend->vkParams.window = this;
      m_backend->initParams = &m_backend->vkParams;
#else
      m_backend->backend = QRhi::Null;
      m_backend->initParams = &m_backend->nullParams;
#endif
      break;
    case QRhi::Metal:
#if QT_CONFIG(metal)
      m_backend->initParams = &m_backend->metalParams;
#else
      m_backend->backend = QRhi::Null;
      m_backend->initParams = &m_backend->nullParams;
#endif
      break;
    case QRhi::D3D11:
#if defined(Q_OS_WIN)
      m_backend->initParams = &m_backend->d3d11Params;
#else
      m_backend->backend = QRhi::Null;
      m_backend->initParams = &m_backend->nullParams;
#endif
      break;
    case QRhi::Null:
    default:
      m_backend->initParams = &m_backend->nullParams;
      break;
  }
  m_rhi = QRhi::create(m_backend->backend, m_backend->initParams);
  if (!m_rhi) {
    qWarning("Failed to create QRhi for HexViewRhiWindow");
    return;
  }

  if (debug) {
    qDebug() << "HexViewRhiWindow init:"
             << "backend=" << int(m_rhi->backend())
             << "surface=" << int(surfaceType())
             << "size=" << size()
             << "dpr=" << devicePixelRatio();
  }

  if (m_renderer) {
    m_renderer->initIfNeeded(m_rhi);
  }

  m_sc = m_rhi->newSwapChain();
  m_sc->setWindow(this);
  m_sc->setSampleCount(1);
  m_rp = m_sc->newCompatibleRenderPassDescriptor();
  m_sc->setRenderPassDescriptor(m_rp);
  resizeSwapChain();
}

void HexViewRhiWindow::resizeSwapChain() {
  if (!m_rhi || !m_sc) {
    return;
  }

  const QSize pixelSize = size() * devicePixelRatio();
  if (pixelSize.isEmpty()) {
    if (debugLoggingEnabled()) {
      qDebug() << "HexViewRhiWindow resizeSwapChain skipped: empty pixel size"
               << "size=" << size()
               << "dpr=" << devicePixelRatio();
    }
    return;
  }

  if (!m_ds || m_ds->pixelSize() != pixelSize) {
    delete m_ds;
    m_ds = m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, m_sc->sampleCount(),
                                  QRhiRenderBuffer::UsedWithSwapChainOnly);
    m_ds->create();
    m_sc->setDepthStencil(m_ds);
  }

  m_hasSwapChain = m_sc->createOrResize();
  if (debugLoggingEnabled()) {
    qDebug() << "HexViewRhiWindow swapchain resized"
             << "pixelSize=" << pixelSize
             << "sampleCount=" << m_sc->sampleCount();
  }
}

void HexViewRhiWindow::renderFrame() {
  if (!isExposed())
    return;

  initIfNeeded();
  if (!m_rhi || !m_sc || !m_view || !m_view->viewport() || !m_renderer)
    return;

  // Apply at most one mouse, wheel move per frame
  drainPendingMouseMove();
  drainPendingWheel();

  const QSize currentPixelSize = m_sc->currentPixelSize();
  if (currentPixelSize.isEmpty()) {
    if (debugLoggingEnabled()) {
      qDebug() << "HexViewRhiWindow renderFrame skipped: swapchain pixel size empty";
    }
    return;
  }

  const QRhi::FrameOpResult result = m_rhi->beginFrame(m_sc);
  if (result == QRhi::FrameOpSwapChainOutOfDate) {
    if (debugLoggingEnabled()) {
      qDebug() << "HexViewRhiWindow frame: swapchain out of date";
    }
    resizeSwapChain();
    if (!m_hasSwapChain)
      return;
    requestUpdate();
    return;
  }
  if (result != QRhi::FrameOpSuccess) {
    if (debugLoggingEnabled()) {
      qDebug() << "HexViewRhiWindow frame: beginFrame failed" << int(result);
    }
    return;
  }

  m_cb = m_sc->currentFrameCommandBuffer();
  QRhiRenderTarget* rt = m_sc->currentFrameRenderTarget();

  HexViewRhiRenderer::RenderTargetInfo info;
  info.renderTarget = rt;
  info.renderPassDesc = m_sc->renderPassDescriptor();
  info.pixelSize = currentPixelSize;
  info.sampleCount = m_sc->sampleCount();
  info.dpr = devicePixelRatio();
  m_renderer->renderFrame(m_cb, info);

  m_rhi->endFrame(m_sc);
}

void HexViewRhiWindow::releaseResources() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }

  delete m_rp;
  m_rp = nullptr;
  delete m_ds;
  m_ds = nullptr;

  releaseSwapChain();
  delete m_sc;
  m_sc = nullptr;

  delete m_rhi;
  m_rhi = nullptr;
}

bool HexViewRhiWindow::debugLoggingEnabled() const {
  return isRhiDebugEnabled();
}
