/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiWindow.h"

#include "HexView.h"
#include "HexViewRhiRenderer.h"
#include "LogManager.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <QDebug>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QExposeEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QScrollBar>

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#endif

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

namespace {
constexpr int SCROLLBAR_FRAME_MS = 16;
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
    : m_view(view),
      m_renderer(renderer),
      m_eventForwarder(view, [this]() { requestUpdate(); }),
      m_backend(std::make_unique<BackendData>()) {
  setFlags(Qt::SubWindow | Qt::FramelessWindowHint);

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

  m_scrollBarFrameTimer.setParent(this);
  m_scrollBarFrameTimer.setInterval(SCROLLBAR_FRAME_MS);
  m_scrollBarFrameTimer.setTimerType(Qt::PreciseTimer);
  connect(&m_scrollBarFrameTimer, &QTimer::timeout, this, [this]() {
    renderFrame();
  });

  if (m_view) {
    if (auto* vbar = m_view->verticalScrollBar()) {
      connect(vbar, &QScrollBar::sliderPressed, this, [this]() {
        if (!m_scrollBarFrameTimer.isActive()) {
          m_scrollBarFrameTimer.start();
        }
      });
      connect(vbar, &QScrollBar::sliderReleased, this, [this]() {
        if (m_scrollBarFrameTimer.isActive()) {
          m_scrollBarFrameTimer.stop();
        }
        requestUpdate();
      });
    }
  }
}

HexViewRhiWindow::~HexViewRhiWindow() {
  releaseResources();
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
    if (m_scrollBarFrameTimer.isActive()) {
      return true;
    }
    renderFrame();
    return true;
  }

  if (m_eventForwarder.handleEvent(e, m_dragging)) {
    return true;
  }

  if (!m_view || !m_view->viewport()) {
    return QWindow::event(e);
  }

  switch (e->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop: {
      switch (e->type()) {
        case QEvent::DragEnter: {
          auto* dragEvent = static_cast<QDragEnterEvent*>(e);
          emit dragOverlayShowRequested();
          dragEvent->acceptProposedAction();
          break;
        }
        case QEvent::DragMove: {
          auto* dragEvent = static_cast<QDragMoveEvent*>(e);
          emit dragOverlayShowRequested();
          dragEvent->acceptProposedAction();
          break;
        }
        case QEvent::DragLeave:
          emit dragOverlayHideRequested();
          e->accept();
          break;
        case QEvent::Drop: {
          auto* dropEvent = static_cast<QDropEvent*>(e);
          emit dropUrlsRequested(dropEvent->mimeData()->urls());
          dropEvent->acceptProposedAction();
          break;
        }
        default:
          break;
      }
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

  L_DEBUG("HexViewRhiWindow init: backend={} surface={} size={}x{} dpr={}",
          int(m_rhi->backend()), int(surfaceType()), size().width(), size().height(),
          devicePixelRatio());

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
    L_DEBUG("HexViewRhiWindow resizeSwapChain skipped: empty pixel size size={}x{} dpr={}",
            size().width(), size().height(), devicePixelRatio());
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
  L_DEBUG("HexViewRhiWindow swapchain resized pixelSize={}x{} sampleCount={}",
          pixelSize.width(), pixelSize.height(), m_sc->sampleCount());
}

void HexViewRhiWindow::renderFrame() {
  if (!isExposed())
    return;

  initIfNeeded();
  if (!m_rhi || !m_sc || !m_view || !m_view->viewport() || !m_renderer)
    return;

  // Apply at most one mouse, wheel move per frame
  m_eventForwarder.drainPendingInput();

  const QSize currentPixelSize = m_sc->currentPixelSize();
  if (currentPixelSize.isEmpty()) {
    L_DEBUG("HexViewRhiWindow renderFrame skipped: swapchain pixel size empty");
    return;
  }

  const QRhi::FrameOpResult result = m_rhi->beginFrame(m_sc);
  if (result == QRhi::FrameOpSwapChainOutOfDate) {
    L_DEBUG("HexViewRhiWindow frame: swapchain out of date");
    resizeSwapChain();
    if (!m_hasSwapChain)
      return;
    requestUpdate();
    return;
  }
  if (result != QRhi::FrameOpSuccess) {
    L_DEBUG("HexViewRhiWindow frame: beginFrame failed {}", int(result));
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
