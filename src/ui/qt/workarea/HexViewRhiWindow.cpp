/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiWindow.h"

#include "HexView.h"
#include "VGMFile.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <rhi/qshader.h>

#include <QColor>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QPalette>
#include <QSize>
#include <QScrollBar>
#include <QString>
#include <QVector4D>
#include <QMouseEvent>

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#endif

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

#include <QParallelAnimationGroup>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <qabstractanimation.h>
#include <qcoreapplication.h>

namespace {
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
const QColor SHADOW_COLOR = Qt::black;

static const float kVertices[] = {
  0.0f, 0.0f,
  1.0f, 0.0f,
  1.0f, 1.0f,
  0.0f, 1.0f,
};
static const quint16 kIndices[] = {0, 1, 2, 0, 2, 3};

static constexpr int kMat4Bytes = 16 * sizeof(float);  // 64
static constexpr int kVec4Bytes = 4 * sizeof(float);   // 16

QVector4D toVec4(const QColor& color) {
  return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

bool isPrintable(uint8_t value) {
  return value >= 0x20 && value <= 0x7E;
}

QShader loadShader(const char* path) {
  QFile file(QString::fromUtf8(path));
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning("Failed to load shader: %s", path);
    return {};
  }
  return QShader::fromSerialized(file.readAll());
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

HexViewRhiWindow::HexViewRhiWindow(HexView* view)
    : m_view(view), m_backend(std::make_unique<BackendData>()) {
  setFlags(Qt::FramelessWindowHint);

#if defined(Q_OS_WIN)
  setSurfaceType(QSurface::Direct3DSurface);
  m_backend->backend = QRhi::D3D11;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
  setSurfaceType(QSurface::MetalSurface);
  m_backend->backend = QRhi::Metal;
#elif QT_CONFIG(opengl)
  setSurfaceType(QSurface::OpenGLSurface);
  m_backend->backend = QRhi::OpenGLES2;
#elif QT_CONFIG(vulkan)
  setSurfaceType(QSurface::VulkanSurface);
  m_backend->backend = QRhi::Vulkan;
#else
  setSurfaceType(QSurface::RasterSurface);
  m_backend->backend = QRhi::Null;
#endif
}

HexViewRhiWindow::~HexViewRhiWindow() {
  releaseResources();
}

void HexViewRhiWindow::markBaseDirty() {
  m_baseDirty = true;
}

void HexViewRhiWindow::markSelectionDirty() {
  m_selectionDirty = true;
}

void HexViewRhiWindow::invalidateCache() {
  m_cachedLines.clear();
  m_cacheStartLine = 0;
  m_cacheEndLine = -1;
  m_baseDirty = true;
  m_selectionDirty = true;
}

// bool HexViewRhiWindow::event(QEvent* e) {
//   if (e->type() == QEvent::UpdateRequest) {
//     renderFrame();
//     return true;
//   }
//   return QWindow::event(e);
// }

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

bool HexViewRhiWindow::event(QEvent *e)
{
  if (e->type() == QEvent::UpdateRequest) {
    renderFrame();

    // Keep pumping frames while dragging (or animating)
    if (m_dragging || m_scrolling || m_pumpFrames > 0) {
      if (m_pumpFrames > 0)
        --m_pumpFrames;
      requestUpdate();
    }
    // Let Qt do its internal handling too
    // return QWindow::event(e);
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

      m_lastScrollTick.restart();
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
  if (m_inited) {
    resizeSwapChain();
  }
  requestUpdate();
}

void HexViewRhiWindow::initIfNeeded() {
  if (m_inited || !m_backend) {
    return;
  }

  if (!m_rhi) {
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
        m_backend->vkInstance = std::make_unique<QVulkanInstance>();
        m_backend->vkInstance->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!m_backend->vkInstance->create()) {
          qWarning("Failed to create QVulkanInstance for HexViewRhiWindow");
          return;
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
  }

  qDebug() << "QRhi backend:" << int(m_rhi->backend())
           << "BaseInstance:" << m_rhi->isFeatureSupported(QRhi::BaseInstance);

  m_sc = m_rhi->newSwapChain();
  m_sc->setWindow(this);
  m_sc->setSampleCount(1);
  m_rp = m_sc->newCompatibleRenderPassDescriptor();
  m_sc->setRenderPassDescriptor(m_rp);
  // m_rp->create();
  resizeSwapChain();

  m_vbuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kVertices));
  m_vbuf->create();
  m_ibuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, sizeof(kIndices));
  m_ibuf->create();

  // const int baseUboSize = m_rhi->ubufAligned(sizeof(QMatrix4x4));
  const int baseUboSize = m_rhi->ubufAligned(kMat4Bytes);
  m_ubuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, baseUboSize);
  m_ubuf->create();

  // const int screenUboSize = m_rhi->ubufAligned(sizeof(QMatrix4x4) + sizeof(QVector4D) * 4);
  const int screenUboSize = m_rhi->ubufAligned(kMat4Bytes + kVec4Bytes * 4);
  m_blurUbufH = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, screenUboSize);
  m_blurUbufH->create();
  m_blurUbufV = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, screenUboSize);
  m_blurUbufV->create();
  m_compositeUbuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, screenUboSize);
  m_compositeUbuf->create();

  m_glyphSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                     QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge);
  m_glyphSampler->create();

  m_maskSampler = m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                    QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge);
  m_maskSampler->create();

  m_staticBuffersUploaded = false;
  m_sampleCount = 0;
  m_inited = true;
}

void HexViewRhiWindow::resizeSwapChain() {
  if (!m_rhi || !m_sc) {
    return;
  }

  const QSize pixelSize = size() * devicePixelRatio();
  if (pixelSize.isEmpty()) {
    return;
  }

  if (!m_ds || m_ds->pixelSize() != pixelSize) {
    delete m_ds;
    m_ds = m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, m_sc->sampleCount(),
                                  QRhiRenderBuffer::UsedWithSwapChainOnly);
    m_ds->create();
    m_sc->setDepthStencil(m_ds);
  }

  m_sc->createOrResize();
}

void HexViewRhiWindow::renderFrame() {
  if (!isExposed())
    return;

  initIfNeeded();
  if (!m_rhi || !m_sc || !m_view || !m_view->m_vgmfile)
    return;

  // Apply at most one mouse, wheel move per frame
  drainPendingMouseMove();
  drainPendingWheel();

  const int viewportWidth = m_view->viewport()->width();
  const int viewportHeight = m_view->viewport()->height();
  if (viewportWidth <= 0 || viewportHeight <= 0 || m_view->m_lineHeight <= 0) {
    return;
  }

  const int totalLines = m_view->getTotalLines();
  if (totalLines <= 0) {
    return;
  }

  if (m_sc->currentPixelSize().isEmpty()) {
    return;
  }

  const QRhi::FrameOpResult result = m_rhi->beginFrame(m_sc);
  if (result == QRhi::FrameOpSwapChainOutOfDate) {
    resizeSwapChain();
    requestUpdate();
    return;
  }
  if (result != QRhi::FrameOpSuccess) {
    return;
  }

  m_cb = m_sc->currentFrameCommandBuffer();
  QRhiRenderTarget* rt = m_sc->currentFrameRenderTarget();

  const int scrollY = m_view->verticalScrollBar()->value();
  const int startLine = std::clamp(scrollY / m_view->m_lineHeight, 0, totalLines - 1);
  const int endLine =
      std::clamp((scrollY + viewportHeight) / m_view->m_lineHeight, 0, totalLines - 1);

  if (startLine != m_lastStartLine || endLine != m_lastEndLine) {
    m_lastStartLine = startLine;
    m_lastEndLine = endLine;
    m_selectionDirty = true;
  }

  m_view->ensureGlyphAtlas(devicePixelRatio());
  ensureCacheWindow(startLine, endLine, totalLines);

  if (m_baseDirty) {
    buildBaseInstances();
    m_baseDirty = false;
    m_baseBufferDirty = true;
  }

  if (m_selectionDirty) {
    buildSelectionInstances(startLine, endLine);
    m_selectionDirty = false;
    m_selectionBufferDirty = true;
  }

  QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
  if (!m_staticBuffersUploaded) {
    u->uploadStaticBuffer(m_vbuf, kVertices);
    u->uploadStaticBuffer(m_ibuf, kIndices);
    m_staticBuffersUploaded = true;
  }
  ensureGlyphTexture(u);
  const QSize pixelSize = m_sc->currentPixelSize();
  ensureRenderTargets(pixelSize);
  ensurePipelines();
  updateUniforms(u, static_cast<float>(scrollY), pixelSize);
  updateInstanceBuffers(u);

  int baseRectFirst = 0;
  int baseRectCount = 0;
  int baseGlyphFirst = 0;
  int baseGlyphCount = 0;
  if (!m_lineRanges.empty()) {
    const int maxIndex = static_cast<int>(m_lineRanges.size()) - 1;
    const int visStart = std::clamp(startLine - m_cacheStartLine, 0, maxIndex);
    const int visEnd = std::clamp(endLine - m_cacheStartLine, 0, maxIndex);
    const LineRange& startRange = m_lineRanges[visStart];
    const LineRange& endRange = m_lineRanges[visEnd];
    baseRectFirst = static_cast<int>(startRange.rectStart);
    const int baseRectLast = static_cast<int>(endRange.rectStart + endRange.rectCount);
    baseRectCount = std::max(0, baseRectLast - baseRectFirst);
    baseGlyphFirst = static_cast<int>(startRange.glyphStart);
    const int baseGlyphLast = static_cast<int>(endRange.glyphStart + endRange.glyphCount);
    baseGlyphCount = std::max(0, baseGlyphLast - baseGlyphFirst);
  }

  const QColor clearColor = m_view->palette().color(QPalette::Window);

  m_cb->beginPass(m_contentRt, clearColor, {1.0f, 0}, u);
  m_cb->setViewport(QRhiViewport(0, 0, pixelSize.width(), pixelSize.height()));
  drawRectBuffer(m_cb, m_baseRectBuf, baseRectCount, baseRectFirst);
  drawGlyphBuffer(m_cb, m_baseGlyphBuf, baseGlyphCount, baseGlyphFirst);
  m_cb->endPass();

  m_cb->beginPass(m_maskRt, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
  m_cb->setViewport(QRhiViewport(0, 0, pixelSize.width(), pixelSize.height()));
  drawRectBuffer(m_cb, m_maskRectBuf, static_cast<int>(m_maskRectInstances.size()), 0, nullptr,
                 m_maskPso);
  m_cb->endPass();

  const bool hasShadow = (m_view->m_shadowBlur > 0.0f) && !m_maskRectInstances.empty();
  if (hasShadow) {
    m_cb->beginPass(m_shadowRtA, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
    m_cb->setViewport(QRhiViewport(0, 0, pixelSize.width(), pixelSize.height()));
    drawFullscreen(m_cb, m_blurPso, m_blurSrbH);
    m_cb->endPass();

    m_cb->beginPass(m_shadowRtB, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
    m_cb->setViewport(QRhiViewport(0, 0, pixelSize.width(), pixelSize.height()));
    drawFullscreen(m_cb, m_blurPso, m_blurSrbV);
    m_cb->endPass();
  } else {
    m_cb->beginPass(m_shadowRtB, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
    m_cb->setViewport(QRhiViewport(0, 0, pixelSize.width(), pixelSize.height()));
    m_cb->endPass();
  }

  m_cb->beginPass(rt, clearColor, {1.0f, 0}, nullptr);
  m_cb->setViewport(QRhiViewport(0, 0, rt->pixelSize().width(), rt->pixelSize().height()));
  drawFullscreen(m_cb, m_compositePso, m_compositeSrb);
  m_cb->endPass();
  m_rhi->endFrame(m_sc);
}

void HexViewRhiWindow::releaseResources() {
  if (m_rhi) {
    m_rhi->makeThreadLocalNativeContextCurrent();
  }

  delete m_rectPso;
  m_rectPso = nullptr;
  delete m_glyphPso;
  m_glyphPso = nullptr;
  delete m_maskPso;
  m_maskPso = nullptr;
  delete m_blurPso;
  m_blurPso = nullptr;
  delete m_compositePso;
  m_compositePso = nullptr;

  delete m_rectSrb;
  m_rectSrb = nullptr;
  delete m_glyphSrb;
  m_glyphSrb = nullptr;
  delete m_blurSrbH;
  m_blurSrbH = nullptr;
  delete m_blurSrbV;
  m_blurSrbV = nullptr;
  delete m_compositeSrb;
  m_compositeSrb = nullptr;

  releaseRenderTargets();

  delete m_glyphTex;
  m_glyphTex = nullptr;
  delete m_glyphSampler;
  m_glyphSampler = nullptr;
  delete m_maskSampler;
  m_maskSampler = nullptr;

  delete m_vbuf;
  m_vbuf = nullptr;
  delete m_ibuf;
  m_ibuf = nullptr;
  delete m_baseRectBuf;
  m_baseRectBuf = nullptr;
  delete m_baseGlyphBuf;
  m_baseGlyphBuf = nullptr;
  delete m_maskRectBuf;
  m_maskRectBuf = nullptr;
  delete m_ubuf;
  m_ubuf = nullptr;
  delete m_blurUbufH;
  m_blurUbufH = nullptr;
  delete m_blurUbufV;
  m_blurUbufV = nullptr;
  delete m_compositeUbuf;
  m_compositeUbuf = nullptr;

  delete m_rp;
  m_rp = nullptr;
  delete m_ds;
  m_ds = nullptr;
  delete m_sc;
  m_sc = nullptr;

  delete m_rhi;
  m_rhi = nullptr;

  m_pipelinesDirty = true;
  m_inited = false;
}

void HexViewRhiWindow::ensureRenderTargets(const QSize& pixelSize) {
  if (!m_rhi || pixelSize.isEmpty()) {
    return;
  }
  if (m_contentTex && m_contentTex->pixelSize() == pixelSize) {
    return;
  }

  releaseRenderTargets();

  auto makeTarget = [&](QRhiTexture** tex, QRhiTextureRenderTarget** rt,
                        QRhiRenderPassDescriptor** rp) {
    *tex = m_rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1, QRhiTexture::RenderTarget);
    (*tex)->create();
    *rt = m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(*tex));
    *rp = (*rt)->newCompatibleRenderPassDescriptor();
    (*rt)->setRenderPassDescriptor(*rp);
    (*rt)->create();
  };

  makeTarget(&m_contentTex, &m_contentRt, &m_contentRp);
  makeTarget(&m_maskTex, &m_maskRt, &m_maskRp);
  makeTarget(&m_shadowTexA, &m_shadowRtA, &m_shadowRpA);
  makeTarget(&m_shadowTexB, &m_shadowRtB, &m_shadowRpB);

  m_pipelinesDirty = true;
}

void HexViewRhiWindow::releaseRenderTargets() {
  delete m_contentRp;
  m_contentRp = nullptr;
  delete m_contentRt;
  m_contentRt = nullptr;
  delete m_contentTex;
  m_contentTex = nullptr;

  delete m_maskRp;
  m_maskRp = nullptr;
  delete m_maskRt;
  m_maskRt = nullptr;
  delete m_maskTex;
  m_maskTex = nullptr;

  delete m_shadowRpA;
  m_shadowRpA = nullptr;
  delete m_shadowRtA;
  m_shadowRtA = nullptr;
  delete m_shadowTexA;
  m_shadowTexA = nullptr;

  delete m_shadowRpB;
  m_shadowRpB = nullptr;
  delete m_shadowRtB;
  m_shadowRtB = nullptr;
  delete m_shadowTexB;
  m_shadowTexB = nullptr;
}

void HexViewRhiWindow::ensurePipelines() {
  if (!m_sc || !m_contentRp || !m_maskRp || !m_shadowRpA || !m_shadowRpB) {
    return;
  }

  const int sampleCount = m_sc->sampleCount();
  if (!m_pipelinesDirty && m_rectPso && m_sampleCount == sampleCount) {
    return;
  }
  m_sampleCount = sampleCount;
  m_pipelinesDirty = false;

  delete m_rectPso;
  delete m_glyphPso;
  delete m_maskPso;
  delete m_blurPso;
  delete m_compositePso;
  delete m_rectSrb;
  delete m_glyphSrb;
  delete m_blurSrbH;
  delete m_blurSrbV;
  delete m_compositeSrb;

  m_rectSrb = m_rhi->newShaderResourceBindings();
  m_rectSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf)
  });
  m_rectSrb->create();

  m_glyphSrb = m_rhi->newShaderResourceBindings();
  m_glyphSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                              m_glyphTex, m_glyphSampler)
  });
  m_glyphSrb->create();

  m_blurSrbH = m_rhi->newShaderResourceBindings();
  m_blurSrbH->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0,
                                             QRhiShaderResourceBinding::VertexStage |
                                             QRhiShaderResourceBinding::FragmentStage,
                                             m_blurUbufH),
    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                              m_maskTex, m_glyphSampler)
  });
  m_blurSrbH->create();

  m_blurSrbV = m_rhi->newShaderResourceBindings();
  m_blurSrbV->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0,
                                             QRhiShaderResourceBinding::VertexStage |
                                             QRhiShaderResourceBinding::FragmentStage,
                                             m_blurUbufV),
    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                              m_shadowTexA, m_glyphSampler)
  });
  m_blurSrbV->create();

  m_compositeSrb = m_rhi->newShaderResourceBindings();
  m_compositeSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0,
                                             QRhiShaderResourceBinding::VertexStage |
                                             QRhiShaderResourceBinding::FragmentStage,
                                             m_compositeUbuf),
    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                              m_contentTex, m_glyphSampler),
    QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                              m_maskTex, m_maskSampler),
    QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                              m_shadowTexB, m_glyphSampler)
  });
  m_compositeSrb->create();

  QShader rectVert = loadShader(":/shaders/hexquad.vert.qsb");
  QShader rectFrag = loadShader(":/shaders/hexquad.frag.qsb");
  QShader glyphVert = loadShader(":/shaders/hexglyph.vert.qsb");
  QShader glyphFrag = loadShader(":/shaders/hexglyph.frag.qsb");
  QShader screenVert = loadShader(":/shaders/hexscreen.vert.qsb");
  QShader blurFrag = loadShader(":/shaders/hexblur.frag.qsb");
  QShader compositeFrag = loadShader(":/shaders/hexcomposite.frag.qsb");

  QRhiVertexInputLayout rectInputLayout;
  rectInputLayout.setBindings({
    {2 * sizeof(float)},
    {sizeof(RectInstance), QRhiVertexInputBinding::PerInstance}
  });
  rectInputLayout.setAttributes({
    {0, 0, QRhiVertexInputAttribute::Float2, 0},
    {1, 1, QRhiVertexInputAttribute::Float4, 0},
    {1, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)}
  });

  QRhiVertexInputLayout glyphInputLayout;
  glyphInputLayout.setBindings({
    {2 * sizeof(float)},
    {sizeof(GlyphInstance), QRhiVertexInputBinding::PerInstance}
  });
  glyphInputLayout.setAttributes({
    {0, 0, QRhiVertexInputAttribute::Float2, 0},
    {1, 1, QRhiVertexInputAttribute::Float4, 0},
    {1, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)},
    {1, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)}
  });

  QRhiVertexInputLayout screenInputLayout;
  screenInputLayout.setBindings({
    {2 * sizeof(float)}
  });
  screenInputLayout.setAttributes({
    {0, 0, QRhiVertexInputAttribute::Float2, 0}
  });

  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::SrcAlpha;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

  m_rectPso = m_rhi->newGraphicsPipeline();
  m_rectPso->setShaderStages({{QRhiShaderStage::Vertex, rectVert},
                              {QRhiShaderStage::Fragment, rectFrag}});
  m_rectPso->setVertexInputLayout(rectInputLayout);
  m_rectPso->setShaderResourceBindings(m_rectSrb);
  m_rectPso->setCullMode(QRhiGraphicsPipeline::None);
  m_rectPso->setTargetBlends({blend});
  m_rectPso->setSampleCount(1);
  m_rectPso->setRenderPassDescriptor(m_contentRp);
  m_rectPso->create();

  m_glyphPso = m_rhi->newGraphicsPipeline();
  m_glyphPso->setShaderStages({{QRhiShaderStage::Vertex, glyphVert},
                               {QRhiShaderStage::Fragment, glyphFrag}});
  m_glyphPso->setVertexInputLayout(glyphInputLayout);
  m_glyphPso->setShaderResourceBindings(m_glyphSrb);
  m_glyphPso->setCullMode(QRhiGraphicsPipeline::None);
  m_glyphPso->setTargetBlends({blend});
  m_glyphPso->setSampleCount(1);
  m_glyphPso->setRenderPassDescriptor(m_contentRp);
  m_glyphPso->create();

  m_maskPso = m_rhi->newGraphicsPipeline();
  m_maskPso->setShaderStages({{QRhiShaderStage::Vertex, rectVert},
                              {QRhiShaderStage::Fragment, rectFrag}});
  m_maskPso->setVertexInputLayout(rectInputLayout);
  m_maskPso->setShaderResourceBindings(m_rectSrb);
  m_maskPso->setCullMode(QRhiGraphicsPipeline::None);
  m_maskPso->setTargetBlends({blend});
  m_maskPso->setSampleCount(1);
  m_maskPso->setRenderPassDescriptor(m_maskRp);
  m_maskPso->create();

  m_blurPso = m_rhi->newGraphicsPipeline();
  m_blurPso->setShaderStages({{QRhiShaderStage::Vertex, screenVert},
                              {QRhiShaderStage::Fragment, blurFrag}});
  m_blurPso->setVertexInputLayout(screenInputLayout);
  m_blurPso->setShaderResourceBindings(m_blurSrbH);
  m_blurPso->setCullMode(QRhiGraphicsPipeline::None);
  m_blurPso->setSampleCount(1);
  m_blurPso->setRenderPassDescriptor(m_shadowRpA);
  m_blurPso->create();

  m_compositePso = m_rhi->newGraphicsPipeline();
  m_compositePso->setShaderStages({{QRhiShaderStage::Vertex, screenVert},
                                   {QRhiShaderStage::Fragment, compositeFrag}});
  m_compositePso->setVertexInputLayout(screenInputLayout);
  m_compositePso->setShaderResourceBindings(m_compositeSrb);
  m_compositePso->setCullMode(QRhiGraphicsPipeline::None);
  m_compositePso->setSampleCount(m_sampleCount);
  m_compositePso->setRenderPassDescriptor(m_sc->renderPassDescriptor());
  m_compositePso->create();
}

void HexViewRhiWindow::ensureGlyphTexture(QRhiResourceUpdateBatch* u) {
  if (!m_view->m_glyphAtlas || m_view->m_glyphAtlas->image.isNull()) {
    return;
  }

  const auto& atlas = *m_view->m_glyphAtlas;
  if (m_glyphTex && m_glyphAtlasVersion == atlas.version) {
    return;
  }

  delete m_glyphTex;
  m_glyphTex = m_rhi->newTexture(QRhiTexture::RGBA8, atlas.image.size(), 1);
  m_glyphTex->create();

  u->uploadTexture(m_glyphTex, atlas.image);

  m_glyphAtlasVersion = atlas.version;

  if (m_glyphSrb) {
    m_glyphSrb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                m_glyphTex, m_glyphSampler)
    });
    m_glyphSrb->create();
  }
}

void HexViewRhiWindow::updateUniforms(QRhiResourceUpdateBatch* u, float scrollY,
                                      const QSize& pixelSize) {
  const QSize viewSize = m_view->viewport()->size();
  const float uvFlipY = m_rhi->isYUpInFramebuffer() ? 0.0f : 1.0f;

  QMatrix4x4 proj;
  proj.ortho(0.f, float(viewSize.width()), float(viewSize.height()), 0.f, -1.f, 1.f);

  QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix() * proj;
  mvp.translate(0.f, -scrollY, 0.f);

  u->updateDynamicBuffer(m_ubuf, 0, kMat4Bytes, mvp.constData());

  QMatrix4x4 screenMvp = m_rhi->clipSpaceCorrMatrix();
  const float invW = pixelSize.width() > 0 ? (1.0f / pixelSize.width()) : 0.0f;
  const float invH = pixelSize.height() > 0 ? (1.0f / pixelSize.height()) : 0.0f;
  const float dpr =
      viewSize.width() > 0 ? (static_cast<float>(pixelSize.width()) / viewSize.width()) : 1.0f;
  const float blurRadius = std::max(0.0f, static_cast<float>(m_view->m_shadowBlur) * dpr);
  const QVector4D blurH(invW, 0.0f, blurRadius, 0.0f);
  const QVector4D blurV(0.0f, invH, blurRadius, 0.0f);

  u->updateDynamicBuffer(m_blurUbufH, 0, kMat4Bytes, screenMvp.constData());
  u->updateDynamicBuffer(m_blurUbufH, kMat4Bytes, kVec4Bytes, &blurH);
  u->updateDynamicBuffer(m_blurUbufV, 0, kMat4Bytes, screenMvp.constData());
  u->updateDynamicBuffer(m_blurUbufV, kMat4Bytes, kVec4Bytes, &blurV);

  const QVector4D screenP2(0.0f, 0.0f, uvFlipY, 0.0f);
  u->updateDynamicBuffer(m_blurUbufH, kMat4Bytes + kVec4Bytes * 2, kVec4Bytes, &screenP2);
  u->updateDynamicBuffer(m_blurUbufV, kMat4Bytes + kVec4Bytes * 2, kVec4Bytes, &screenP2);


  const float viewW = static_cast<float>(viewSize.width());
  const float viewH = static_cast<float>(viewSize.height());
  const float shadowStrength = m_view->m_shadowBlur > 0.0f ? 1.0f : 0.0f;
  const float shadowUvX = viewW > 0.0f ? (m_view->m_shadowOffset.x() / viewW) : 0.0f;
  const float shadowUvY = viewH > 0.0f ? (m_view->m_shadowOffset.y() / viewH) : 0.0f;

  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float hexStartX = static_cast<float>(m_view->hexXOffset()) - charHalfWidth;
  const float hexWidth = kBytesPerLine * 3.0f * charWidth;
  const float asciiStartX =
      static_cast<float>(m_view->hexXOffset()) +
      (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const float asciiWidth =
      m_view->m_shouldDrawAscii ? (kBytesPerLine * charWidth) : 0.0f;

  const QVector4D p0(static_cast<float>(m_view->m_overlayOpacity), shadowStrength,
                     shadowUvX, shadowUvY);
  const QVector4D p1(hexStartX, hexWidth, asciiStartX, asciiWidth);
  const QVector4D p2(viewW, viewH, uvFlipY, 0.0f);
  const QVector4D p3 = toVec4(SHADOW_COLOR);

  u->updateDynamicBuffer(m_compositeUbuf, 0, kMat4Bytes, screenMvp.constData());
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 0, kVec4Bytes, &p0);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 1, kVec4Bytes, &p1);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 2, kVec4Bytes, &p2);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 3, kVec4Bytes, &p3);
}

bool HexViewRhiWindow::ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes) {
  if (bytes <= 0) {
    bytes = 1;
  }
  if (!buffer || buffer->size() < static_cast<quint32>(bytes)) {
    delete buffer;
    buffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes);
    buffer->create();
    return true;
  }
  return false;
}

void HexViewRhiWindow::updateInstanceBuffers(QRhiResourceUpdateBatch* u) {
  bool baseRectResized = ensureInstanceBuffer(
      m_baseRectBuf, static_cast<int>(m_baseRectInstances.size() * sizeof(RectInstance)));
  if ((m_baseBufferDirty || baseRectResized) && !m_baseRectInstances.empty()) {
    u->updateDynamicBuffer(m_baseRectBuf, 0,
                           static_cast<int>(m_baseRectInstances.size() * sizeof(RectInstance)),
                           m_baseRectInstances.data());
  }

  bool baseGlyphResized = ensureInstanceBuffer(
      m_baseGlyphBuf, static_cast<int>(m_baseGlyphInstances.size() * sizeof(GlyphInstance)));
  if ((m_baseBufferDirty || baseGlyphResized) && !m_baseGlyphInstances.empty()) {
    u->updateDynamicBuffer(m_baseGlyphBuf, 0,
                           static_cast<int>(m_baseGlyphInstances.size() * sizeof(GlyphInstance)),
                           m_baseGlyphInstances.data());
  }

  bool maskRectResized = ensureInstanceBuffer(
      m_maskRectBuf, static_cast<int>(m_maskRectInstances.size() * sizeof(RectInstance)));
  if ((m_selectionBufferDirty || maskRectResized) && !m_maskRectInstances.empty()) {
    u->updateDynamicBuffer(m_maskRectBuf, 0,
                           static_cast<int>(m_maskRectInstances.size() * sizeof(RectInstance)),
                           m_maskRectInstances.data());
  }

  m_baseBufferDirty = false;
  m_selectionBufferDirty = false;
}

void HexViewRhiWindow::drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                                     int firstInstance, QRhiShaderResourceBindings* srb,
                                     QRhiGraphicsPipeline* pso) {
  if (!buffer || count <= 0) {
    return;
  }
  if (!srb) {
    srb = m_rectSrb;
  }
  if (!pso) {
    pso = m_rectPso;
  }
  cb->setGraphicsPipeline(pso);
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {buffer, 0}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, count, 0, 0, firstInstance);
}

void HexViewRhiWindow::drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                                      int firstInstance) {
  if (!buffer || count <= 0) {
    return;
  }
  cb->setGraphicsPipeline(m_glyphPso);
  cb->setShaderResources(m_glyphSrb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {buffer, 0}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, count, 0, 0, firstInstance);
}

void HexViewRhiWindow::drawFullscreen(QRhiCommandBuffer* cb, QRhiGraphicsPipeline* pso,
                                      QRhiShaderResourceBindings* srb) {
  if (!pso || !srb) {
    return;
  }
  cb->setGraphicsPipeline(pso);
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0}
  };
  cb->setVertexInput(0, 1, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, 1, 0, 0, 0);
}

QRectF HexViewRhiWindow::glyphUv(const QChar& ch) const {
  if (!m_view->m_glyphAtlas) {
    return {};
  }
  const auto& uvTable = m_view->m_glyphAtlas->uvTable;
  const ushort code = ch.unicode();
  if (code < uvTable.size() && !uvTable[code].isNull()) {
    return uvTable[code];
  }
  const ushort fallback = static_cast<ushort>('.');
  if (fallback < uvTable.size()) {
    return uvTable[fallback];
  }
  return {};
}

void HexViewRhiWindow::appendRect(std::vector<RectInstance>& rects, float x, float y, float w, float h,
                                 const QVector4D& color) {
  rects.push_back({x, y, w, h, color.x(), color.y(), color.z(), color.w()});
}

void HexViewRhiWindow::appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w, float h,
                                  const QRectF& uv, const QVector4D& color) {
  if (uv.isNull()) {
    return;
  }
  glyphs.push_back({x, y, w, h,
                    static_cast<float>(uv.left()), static_cast<float>(uv.top()),
                    static_cast<float>(uv.right()), static_cast<float>(uv.bottom()),
                    color.x(), color.y(), color.z(), color.w()});
}

void HexViewRhiWindow::ensureCacheWindow(int startLine, int endLine, int totalLines) {
  const bool needsWindow =
      m_cachedLines.empty() || startLine < m_cacheStartLine || endLine > m_cacheEndLine;
  if (!needsWindow) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  const int margin = std::max(visibleCount * 2, 1);
  m_cacheStartLine = std::max(0, startLine - margin);
  m_cacheEndLine = std::min(totalLines - 1, endLine + margin);
  rebuildCacheWindow();
  m_baseDirty = true;
  m_selectionDirty = true;
}

void HexViewRhiWindow::rebuildCacheWindow() {
  m_cachedLines.clear();
  if (m_cacheEndLine < m_cacheStartLine) {
    return;
  }

  const uint32_t fileLength = m_view->m_vgmfile->unLength;
  const auto* baseData = reinterpret_cast<const uint8_t*>(m_view->m_vgmfile->data());
  const size_t count = static_cast<size_t>(m_cacheEndLine - m_cacheStartLine + 1);
  m_cachedLines.reserve(count);

  for (int line = m_cacheStartLine; line <= m_cacheEndLine; ++line) {
    CachedLine entry{};
    entry.line = line;
    const int lineOffset = line * kBytesPerLine;
    if (lineOffset < static_cast<int>(fileLength)) {
      entry.bytes = std::min(kBytesPerLine, static_cast<int>(fileLength) - lineOffset);
      if (entry.bytes > 0) {
        std::copy_n(baseData + lineOffset, entry.bytes, entry.data.data());
        for (int i = 0; i < entry.bytes; ++i) {
          const int idx = lineOffset + i;
          entry.styles[i] =
              (idx >= 0 && idx < static_cast<int>(m_view->m_styleIds.size()))
                  ? m_view->m_styleIds[idx]
                  : 0;
        }
      }
    }
    m_cachedLines.push_back(entry);
  }
}

const HexViewRhiWindow::CachedLine* HexViewRhiWindow::cachedLineFor(int line) const {
  if (line < m_cacheStartLine || line > m_cacheEndLine) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(line - m_cacheStartLine);
  if (index >= m_cachedLines.size()) {
    return nullptr;
  }
  return &m_cachedLines[index];
}

void HexViewRhiWindow::buildBaseInstances() {
  m_baseRectInstances.clear();
  m_baseGlyphInstances.clear();
  m_lineRanges.clear();

  if (m_cachedLines.empty()) {
    return;
  }

  m_baseRectInstances.reserve(m_cachedLines.size() * 4);
  m_baseGlyphInstances.reserve(m_cachedLines.size() * (m_view->m_shouldDrawAscii ? 80 : 64));
  m_lineRanges.reserve(m_cachedLines.size());

  const QColor clearColor = m_view->palette().color(QPalette::Window);
  const QVector4D addressColor = toVec4(m_view->palette().color(QPalette::WindowText));

  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float lineHeight = static_cast<float>(m_view->m_lineHeight);

  const float hexStartX = static_cast<float>(m_view->hexXOffset());
  const float asciiStartX =
      hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;

  const auto& styles = m_view->m_styles;
  auto styleFor = [&](uint16_t styleId) -> const HexView::Style& {
    if (styleId < styles.size()) {
      return styles[styleId];
    }
    return styles.front();
  };

  static const char kHexDigits[] = "0123456789ABCDEF";
  std::array<QRectF, 16> hexUvs{};
  for (int i = 0; i < 16; ++i) {
    hexUvs[i] = glyphUv(QLatin1Char(kHexDigits[i]));
  }
  const QRectF spaceUv = glyphUv(QLatin1Char(' '));

  for (const auto& entry : m_cachedLines) {
    LineRange range{};
    range.rectStart = static_cast<uint32_t>(m_baseRectInstances.size());
    range.glyphStart = static_cast<uint32_t>(m_baseGlyphInstances.size());
    const float y = entry.line * lineHeight;

    if (m_view->m_shouldDrawOffset) {
      uint32_t address = m_view->m_vgmfile->dwOffset + (entry.line * kBytesPerLine);
      if (m_view->m_addressAsHex) {
        char buf[NUM_ADDRESS_NIBBLES];
        for (int i = NUM_ADDRESS_NIBBLES - 1; i >= 0; --i) {
          buf[i] = kHexDigits[address & 0xF];
          address >>= 4;
        }
        for (int i = 0; i < NUM_ADDRESS_NIBBLES; ++i) {
          appendGlyph(m_baseGlyphInstances, i * charWidth, y, charWidth, lineHeight,
                      glyphUv(QLatin1Char(buf[i])), addressColor);
        }
      } else {
        QString text = QString::number(address).rightJustified(NUM_ADDRESS_NIBBLES, QLatin1Char('0'));
        for (int i = 0; i < text.size(); ++i) {
          appendGlyph(m_baseGlyphInstances, i * charWidth, y, charWidth, lineHeight,
                      glyphUv(text[i]), addressColor);
        }
      }
    }

    if (entry.bytes <= 0) {
      continue;
    }

    int spanStart = 0;
    uint16_t spanStyle = entry.styles[0];
    for (int i = 1; i <= entry.bytes; ++i) {
      if (i == entry.bytes || entry.styles[i] != spanStyle) {
        const int spanLen = i - spanStart;
        const auto& style = styleFor(spanStyle);
        if (style.bg != clearColor) {
          const QVector4D bgColor = toVec4(style.bg);
          const float hexX = hexStartX + spanStart * 3.0f * charWidth - charHalfWidth;
          appendRect(m_baseRectInstances, hexX, y, spanLen * 3.0f * charWidth, lineHeight, bgColor);
          if (m_view->m_shouldDrawAscii) {
            const float asciiX = asciiStartX + spanStart * charWidth;
            appendRect(m_baseRectInstances, asciiX, y, spanLen * charWidth, lineHeight, bgColor);
          }
        }
        if (i < entry.bytes) {
          spanStart = i;
          spanStyle = entry.styles[i];
        }
      }
    }

    for (int i = 0; i < entry.bytes; ++i) {
      const auto& style = styleFor(entry.styles[i]);
      const QVector4D textColor = toVec4(style.fg);

      const uint8_t value = entry.data[i];
      const float hexX = hexStartX + i * 3.0f * charWidth;
      appendGlyph(m_baseGlyphInstances, hexX, y, charWidth, lineHeight, hexUvs[value >> 4], textColor);
      appendGlyph(m_baseGlyphInstances, hexX + charWidth, y, charWidth, lineHeight,
                  hexUvs[value & 0x0F], textColor);
      appendGlyph(m_baseGlyphInstances, hexX + 2.0f * charWidth, y, charWidth, lineHeight,
                  spaceUv, textColor);

      if (m_view->m_shouldDrawAscii) {
        const char asciiChar = isPrintable(value) ? static_cast<char>(value) : '.';
        const float asciiX = asciiStartX + i * charWidth;
        appendGlyph(m_baseGlyphInstances, asciiX, y, charWidth, lineHeight,
                    glyphUv(QLatin1Char(asciiChar)), textColor);
      }
    }

    range.rectCount = static_cast<uint32_t>(m_baseRectInstances.size()) - range.rectStart;
    range.glyphCount = static_cast<uint32_t>(m_baseGlyphInstances.size()) - range.glyphStart;
    m_lineRanges.push_back(range);
  }
}

void HexViewRhiWindow::buildSelectionInstances(int startLine, int endLine) {
  m_maskRectInstances.clear();

  const bool hasSelection = !m_view->m_selections.empty() || !m_view->m_fadeSelections.empty();
  if (!hasSelection || startLine > endLine) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount <= 0) {
    return;
  }

  m_maskRectInstances.reserve(static_cast<size_t>(visibleCount) *
                              (m_view->m_shouldDrawAscii ? 4 : 2));

  const uint32_t fileLength = m_view->m_vgmfile->unLength;
  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float lineHeight = static_cast<float>(m_view->m_lineHeight);
  const float hexStartX = static_cast<float>(m_view->hexXOffset());
  const float asciiStartX =
      hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const QVector4D maskColor(1.0f, 1.0f, 1.0f, 1.0f);

  struct Interval {
    int start = 0;
    int end = 0;
  };

  std::vector<std::vector<Interval>> perLine;
  perLine.resize(static_cast<size_t>(visibleCount));

  const std::vector<HexView::SelectionRange>& selections =
      m_view->m_selections.empty() ? m_view->m_fadeSelections : m_view->m_selections;

  for (const auto& selection : selections) {
    if (selection.length == 0) {
      continue;
    }

    int selectionStart = static_cast<int>(selection.offset - m_view->m_vgmfile->dwOffset);
    int selectionEnd = selectionStart + static_cast<int>(selection.length);
    if (selectionEnd <= 0 || selectionStart >= static_cast<int>(fileLength)) {
      continue;
    }
    selectionStart = std::max(selectionStart, 0);
    selectionEnd = std::min(selectionEnd, static_cast<int>(fileLength));
    if (selectionEnd <= selectionStart) {
      continue;
    }

    const int selStartLine = selectionStart / kBytesPerLine;
    const int selEndLine = (selectionEnd - 1) / kBytesPerLine;
    const int lineStart = std::max(startLine, selStartLine);
    const int lineEnd = std::min(endLine, selEndLine);

    for (int line = lineStart; line <= lineEnd; ++line) {
      const CachedLine* entry = cachedLineFor(line);
      if (!entry || entry->bytes <= 0) {
        continue;
      }

      const int lineOffset = line * kBytesPerLine;
      const int startCol = (line == selStartLine) ? (selectionStart - lineOffset) : 0;
      const int endCol = (line == selEndLine) ? (selectionEnd - lineOffset) : entry->bytes;

      const int clampedStart = std::clamp(startCol, 0, entry->bytes);
      const int clampedEnd = std::clamp(endCol, 0, entry->bytes);
      if (clampedEnd <= clampedStart) {
        continue;
      }

      perLine[static_cast<size_t>(line - startLine)].push_back(
          {clampedStart, clampedEnd});
    }
  }

  for (int line = startLine; line <= endLine; ++line) {
    auto& intervals = perLine[static_cast<size_t>(line - startLine)];
    if (intervals.empty()) {
      continue;
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });

    int curStart = intervals.front().start;
    int curEnd = intervals.front().end;
    const float y = line * lineHeight;

    auto emitRect = [&](int col0, int col1) {
      const int length = col1 - col0;
      if (length <= 0) {
        return;
      }
      const float hexX = hexStartX + col0 * 3.0f * charWidth - charHalfWidth;
      appendRect(m_maskRectInstances, hexX, y, length * 3.0f * charWidth, lineHeight, maskColor);
      if (m_view->m_shouldDrawAscii) {
        const float asciiX = asciiStartX + col0 * charWidth;
        appendRect(m_maskRectInstances, asciiX, y, length * charWidth, lineHeight, maskColor);
      }
    };

    for (size_t i = 1; i < intervals.size(); ++i) {
      if (intervals[i].start <= curEnd) {
        curEnd = std::max(curEnd, intervals[i].end);
        continue;
      }
      emitRect(curStart, curEnd);
      curStart = intervals[i].start;
      curEnd = intervals[i].end;
    }
    emitRect(curStart, curEnd);
  }
}
