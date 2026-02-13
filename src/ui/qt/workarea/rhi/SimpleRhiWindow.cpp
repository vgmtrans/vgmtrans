/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SimpleRhiWindow.h"

#include "RhiWindowDragDropEvents.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <QEvent>
#include <QExposeEvent>
#include <QResizeEvent>

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#endif

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

struct SimpleRhiWindow::BackendData {
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

SimpleRhiWindow::SimpleRhiWindow()
    : m_backend(std::make_unique<BackendData>()) {
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
      qWarning("Failed to create QVulkanInstance for SimpleRhiWindow");
      m_backend->vkInstance.reset();
      setSurfaceType(QSurface::RasterSurface);
      m_backend->backend = QRhi::Null;
    } else {
      setVulkanInstance(m_backend->vkInstance.get());
    }
  }
#endif
}

SimpleRhiWindow::~SimpleRhiWindow() {
  // Base destructor must avoid virtual callbacks; derived destructors call
  // releaseRhiResources() first while dynamic dispatch is still valid.
  releaseResources(false);
}

bool SimpleRhiWindow::event(QEvent* e) {
  if (e && e->type() == QEvent::UpdateRequest) {
    renderFrame();
    return true;
  }

  if (e && handleWindowEvent(e)) {
    return true;
  }

  if (QtUi::handleRhiWindowDragDropEvent(
          e,
          [this]() { emit dragOverlayShowRequested(); },
          [this]() { emit dragOverlayHideRequested(); },
          [this](const QList<QUrl>& urls) { emit dropUrlsRequested(urls); })) {
    return true;
  }

  return QWindow::event(e);
}

void SimpleRhiWindow::exposeEvent(QExposeEvent* event) {
  Q_UNUSED(event);
  if (!isExposed()) {
    return;
  }
  initIfNeeded();
  requestUpdate();
}

void SimpleRhiWindow::resizeEvent(QResizeEvent* event) {
  Q_UNUSED(event);
  if (m_rhi) {
    resizeSwapChain();
  }
  requestUpdate();
}

bool SimpleRhiWindow::handleWindowEvent(QEvent* e) {
  Q_UNUSED(e);
  return false;
}

void SimpleRhiWindow::initIfNeeded() {
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
    qWarning("Failed to create QRhi for SimpleRhiWindow");
    return;
  }

  onRhiInitialized(m_rhi);

  m_sc = m_rhi->newSwapChain();
  m_sc->setWindow(this);
  m_sc->setSampleCount(1);
  m_rp = m_sc->newCompatibleRenderPassDescriptor();
  m_sc->setRenderPassDescriptor(m_rp);
  resizeSwapChain();
}

void SimpleRhiWindow::resizeSwapChain() {
  if (!m_rhi || !m_sc) {
    return;
  }

  const QSize pixelSize = size() * devicePixelRatio();
  if (pixelSize.isEmpty()) {
    return;
  }

  if (!m_ds || m_ds->pixelSize() != pixelSize) {
    delete m_ds;
    m_ds = m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil,
                                  pixelSize,
                                  m_sc->sampleCount(),
                                  QRhiRenderBuffer::UsedWithSwapChainOnly);
    m_ds->create();
    m_sc->setDepthStencil(m_ds);
  }

  m_hasSwapChain = m_sc->createOrResize();
}

void SimpleRhiWindow::renderFrame() {
  if (!isExposed()) {
    return;
  }

  initIfNeeded();
  if (!m_rhi || !m_sc) {
    return;
  }

  const QSize currentPixelSize = m_sc->currentPixelSize();
  if (currentPixelSize.isEmpty()) {
    return;
  }

  const QRhi::FrameOpResult result = m_rhi->beginFrame(m_sc);
  if (result == QRhi::FrameOpSwapChainOutOfDate) {
    resizeSwapChain();
    if (!m_hasSwapChain) {
      return;
    }
    requestUpdate();
    return;
  }
  if (result != QRhi::FrameOpSuccess) {
    return;
  }

  QRhiCommandBuffer* cb = m_sc->currentFrameCommandBuffer();
  QRhiRenderTarget* renderTarget = m_sc->currentFrameRenderTarget();
  renderRhiFrame(cb,
                 renderTarget,
                 m_sc->renderPassDescriptor(),
                 currentPixelSize,
                 m_sc->sampleCount(),
                 devicePixelRatio());

  m_rhi->endFrame(m_sc);
}

void SimpleRhiWindow::releaseSwapChain() {
  if (m_hasSwapChain && m_sc) {
    m_hasSwapChain = false;
    m_sc->destroy();
  }
}

void SimpleRhiWindow::releaseRhiResources() {
  releaseResources(true);
}

void SimpleRhiWindow::releaseResources(bool notifyDerived) {
  if (m_resourcesReleased) {
    return;
  }

  if (notifyDerived && m_rhi) {
    onRhiReleased();
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
  m_resourcesReleased = true;
}
