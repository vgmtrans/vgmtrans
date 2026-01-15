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
#include <QScrollBar>
#include <QString>
#include <QVector4D>

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
#include <qabstractanimation.h>
#include <qcoreapplication.h>

namespace {
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 18;
const QColor SHADOW_COLOR = Qt::black;
constexpr float SHADOW_CORNER_RADIUS = 0.0f;

static const float kVertices[] = {
  0.0f, 0.0f,
  1.0f, 0.0f,
  1.0f, 1.0f,
  0.0f, 1.0f,
};
static const quint16 kIndices[] = {0, 1, 2, 0, 2, 3};

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
//
// #if QT_CONFIG(vulkan)
//   QRhiVulkanInitParams vkParams;
//   std::unique_ptr<QVulkanInstance> vkInstance;
// #endif

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

void HexViewRhiWindow::markOverlayDirty() {
  m_overlayDirty = true;
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

bool HexViewRhiWindow::event(QEvent *e)
{
  // Keep your UpdateRequest/render handling first
  if (e->type() == QEvent::UpdateRequest) {
    renderFrame();

    // const bool animating =
    // m_view && m_view->m_selectionAnimation &&
    // m_view->m_selectionAnimation->state() == QAbstractAnimation::Running;

    // if (m_view && (m_view->m_isDragging || animating)) {
    //   requestUpdate();
    // }
    return true;
  }

  if (!m_view || !m_view->viewport())
    return QWindow::event(e);

  switch (e->type()) {
    case QEvent::MouseButtonPress:
      // Ensure HexView gets keyboard focus after click
      m_view->setFocus(Qt::MouseFocusReason);
      QCoreApplication::sendEvent(m_view->viewport(), e);
      return true;

    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
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
// #if QT_CONFIG(vulkan)
//         m_backend->vkInstance = std::make_unique<QVulkanInstance>();
//         m_backend->vkInstance->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
//         if (!m_backend->vkInstance->create()) {
//           qWarning("Failed to create QVulkanInstance for HexViewRhiWindow");
//           return;
//         }
//         m_backend->vkParams.inst = m_backend->vkInstance.get();
//         m_backend->vkParams.window = this;
//         m_backend->initParams = &m_backend->vkParams;
// #else
        m_backend->backend = QRhi::Null;
        m_backend->initParams = &m_backend->nullParams;
// #endif
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

  const int uboSize = m_rhi->ubufAligned(sizeof(QMatrix4x4) + sizeof(QVector4D));
  m_ubuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uboSize);
  m_ubuf->create();

  m_overlayUbuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                   sizeof(QMatrix4x4));
  m_overlayUbuf->create();

  m_shadowUbuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                  sizeof(QMatrix4x4) + sizeof(QVector4D));
  m_shadowUbuf->create();

  m_glyphSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                     QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge);
  m_glyphSampler->create();

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
  if (!isExposed()) {
    return;
  }
  initIfNeeded();
  if (!m_rhi || !m_sc || !m_view || !m_view->m_vgmfile) {
    return;
  }

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

  if (m_overlayDirty) {
    buildOverlayInstances(viewportHeight);
    m_overlayDirty = false;
    m_overlayBufferDirty = true;
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
  ensurePipelines();
  updateUniforms(u, static_cast<float>(scrollY));
  updateInstanceBuffers(u);

  m_cb->beginPass(rt, m_view->palette().color(QPalette::Window), {1.0f, 0}, u);
  m_cb->setViewport(QRhiViewport(0, 0, rt->pixelSize().width(), rt->pixelSize().height()));

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

  drawRectBuffer(m_cb, m_baseRectBuf, baseRectCount, baseRectFirst);
  drawGlyphBuffer(m_cb, m_baseGlyphBuf, baseGlyphCount, baseGlyphFirst);
  drawRectBuffer(m_cb, m_overlayRectBuf, static_cast<int>(m_overlayRectInstances.size()), 0,
                 m_overlaySrb);
  drawShadowBuffer(m_cb);
  drawRectBuffer(m_cb, m_selectionRectBuf, static_cast<int>(m_selectionRectInstances.size()));
  drawGlyphBuffer(m_cb, m_selectionGlyphBuf, static_cast<int>(m_selectionGlyphInstances.size()));

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
  delete m_shadowPso;
  m_shadowPso = nullptr;

  delete m_rectSrb;
  m_rectSrb = nullptr;
  delete m_overlaySrb;
  m_overlaySrb = nullptr;
  delete m_glyphSrb;
  m_glyphSrb = nullptr;
  delete m_shadowSrb;
  m_shadowSrb = nullptr;

  delete m_glyphTex;
  m_glyphTex = nullptr;
  delete m_glyphSampler;
  m_glyphSampler = nullptr;

  delete m_vbuf;
  m_vbuf = nullptr;
  delete m_ibuf;
  m_ibuf = nullptr;
  delete m_baseRectBuf;
  m_baseRectBuf = nullptr;
  delete m_baseGlyphBuf;
  m_baseGlyphBuf = nullptr;
  delete m_overlayRectBuf;
  m_overlayRectBuf = nullptr;
  delete m_selectionRectBuf;
  m_selectionRectBuf = nullptr;
  delete m_selectionGlyphBuf;
  m_selectionGlyphBuf = nullptr;
  delete m_shadowBuf;
  m_shadowBuf = nullptr;
  delete m_ubuf;
  m_ubuf = nullptr;
  delete m_overlayUbuf;
  m_overlayUbuf = nullptr;
  delete m_shadowUbuf;
  m_shadowUbuf = nullptr;

  delete m_rp;
  m_rp = nullptr;
  delete m_ds;
  m_ds = nullptr;
  delete m_sc;
  m_sc = nullptr;

  delete m_rhi;
  m_rhi = nullptr;

  m_inited = false;
}

void HexViewRhiWindow::ensurePipelines() {
  if (!m_sc) {
    return;
  }

  const int sampleCount = m_sc->sampleCount();
  if (m_rectPso && m_sampleCount == sampleCount) {
    return;
  }
  m_sampleCount = sampleCount;

  delete m_rectPso;
  delete m_glyphPso;
  delete m_shadowPso;
  delete m_rectSrb;
  delete m_overlaySrb;
  delete m_glyphSrb;
  delete m_shadowSrb;

  m_rectSrb = m_rhi->newShaderResourceBindings();
  m_rectSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf)
  });
  m_rectSrb->create();

  m_overlaySrb = m_rhi->newShaderResourceBindings();
  m_overlaySrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_overlayUbuf)
  });
  m_overlaySrb->create();

  m_glyphSrb = m_rhi->newShaderResourceBindings();
  m_glyphSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                              m_glyphTex, m_glyphSampler)
  });
  m_glyphSrb->create();

  m_shadowSrb = m_rhi->newShaderResourceBindings();
  m_shadowSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0,
                                             QRhiShaderResourceBinding::VertexStage |
                                             QRhiShaderResourceBinding::FragmentStage,
                                             m_shadowUbuf)
  });
  m_shadowSrb->create();

  QShader rectVert = loadShader(":/shaders/hexquad.vert.qsb");
  QShader rectFrag = loadShader(":/shaders/hexquad.frag.qsb");
  QShader glyphVert = loadShader(":/shaders/hexglyph.vert.qsb");
  QShader glyphFrag = loadShader(":/shaders/hexglyph.frag.qsb");
  QShader shadowVert = loadShader(":/shaders/hexshadow.vert.qsb");
  QShader shadowFrag = loadShader(":/shaders/hexshadow.frag.qsb");

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

  QRhiVertexInputLayout shadowInputLayout;
  shadowInputLayout.setBindings({
    {2 * sizeof(float)},
    {sizeof(ShadowInstance), QRhiVertexInputBinding::PerInstance}
  });
  shadowInputLayout.setAttributes({
    {0, 0, QRhiVertexInputAttribute::Float2, 0},
    {1, 1, QRhiVertexInputAttribute::Float4, 0},
    {1, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)},
    {1, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)}
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
  m_rectPso->setSampleCount(m_sampleCount);
  m_rectPso->setRenderPassDescriptor(m_sc->renderPassDescriptor());
  m_rectPso->create();

  m_glyphPso = m_rhi->newGraphicsPipeline();
  m_glyphPso->setShaderStages({{QRhiShaderStage::Vertex, glyphVert},
                               {QRhiShaderStage::Fragment, glyphFrag}});
  m_glyphPso->setVertexInputLayout(glyphInputLayout);
  m_glyphPso->setShaderResourceBindings(m_glyphSrb);
  m_glyphPso->setCullMode(QRhiGraphicsPipeline::None);
  m_glyphPso->setTargetBlends({blend});
  m_glyphPso->setSampleCount(m_sampleCount);
  m_glyphPso->setRenderPassDescriptor(m_sc->renderPassDescriptor());
  m_glyphPso->create();

  m_shadowPso = m_rhi->newGraphicsPipeline();
  m_shadowPso->setShaderStages({{QRhiShaderStage::Vertex, shadowVert},
                                {QRhiShaderStage::Fragment, shadowFrag}});
  m_shadowPso->setVertexInputLayout(shadowInputLayout);
  m_shadowPso->setShaderResourceBindings(m_shadowSrb);
  m_shadowPso->setCullMode(QRhiGraphicsPipeline::None);
  m_shadowPso->setTargetBlends({blend});
  m_shadowPso->setSampleCount(m_sampleCount);
  m_shadowPso->setRenderPassDescriptor(m_sc->renderPassDescriptor());
  m_shadowPso->create();
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

void HexViewRhiWindow::updateUniforms(QRhiResourceUpdateBatch* u, float scrollY) {
  const QSize sz = m_view->viewport()->size();

  QMatrix4x4 proj;
  proj.ortho(0.f, float(sz.width()), float(sz.height()), 0.f, -1.f, 1.f);

  QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix() * proj;
  mvp.translate(0.f, -scrollY, 0.f);

  QVector4D params(0.f, 0.f, 0.f, 0.f);

  u->updateDynamicBuffer(m_ubuf, 0, sizeof(QMatrix4x4), &mvp);
  u->updateDynamicBuffer(m_ubuf, sizeof(QMatrix4x4), sizeof(QVector4D), &params);

  QMatrix4x4 overlayMvp = m_rhi->clipSpaceCorrMatrix() * proj;
  u->updateDynamicBuffer(m_overlayUbuf, 0, sizeof(QMatrix4x4), &overlayMvp);

  u->updateDynamicBuffer(m_shadowUbuf, 0, sizeof(QMatrix4x4), &mvp);
  QVector4D shadowParams(m_view->m_shadowOffset.x(), m_view->m_shadowOffset.y(),
                         m_view->m_shadowBlur, SHADOW_CORNER_RADIUS);
  u->updateDynamicBuffer(m_shadowUbuf, sizeof(QMatrix4x4), sizeof(QVector4D), &shadowParams);
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

  bool overlayResized = ensureInstanceBuffer(
      m_overlayRectBuf, static_cast<int>(m_overlayRectInstances.size() * sizeof(RectInstance)));
  if ((m_overlayBufferDirty || overlayResized) && !m_overlayRectInstances.empty()) {
    u->updateDynamicBuffer(m_overlayRectBuf, 0,
                           static_cast<int>(m_overlayRectInstances.size() * sizeof(RectInstance)),
                           m_overlayRectInstances.data());
  }

  bool selectionRectResized = ensureInstanceBuffer(
      m_selectionRectBuf, static_cast<int>(m_selectionRectInstances.size() * sizeof(RectInstance)));
  if ((m_selectionBufferDirty || selectionRectResized) && !m_selectionRectInstances.empty()) {
    u->updateDynamicBuffer(m_selectionRectBuf, 0,
                           static_cast<int>(m_selectionRectInstances.size() * sizeof(RectInstance)),
                           m_selectionRectInstances.data());
  }

  bool selectionGlyphResized = ensureInstanceBuffer(
      m_selectionGlyphBuf, static_cast<int>(m_selectionGlyphInstances.size() * sizeof(GlyphInstance)));
  if ((m_selectionBufferDirty || selectionGlyphResized) && !m_selectionGlyphInstances.empty()) {
    u->updateDynamicBuffer(m_selectionGlyphBuf, 0,
                           static_cast<int>(m_selectionGlyphInstances.size() * sizeof(GlyphInstance)),
                           m_selectionGlyphInstances.data());
  }

  bool shadowResized = ensureInstanceBuffer(
      m_shadowBuf, static_cast<int>(m_shadowInstances.size() * sizeof(ShadowInstance)));
  if ((m_selectionBufferDirty || shadowResized) && !m_shadowInstances.empty()) {
    u->updateDynamicBuffer(m_shadowBuf, 0,
                           static_cast<int>(m_shadowInstances.size() * sizeof(ShadowInstance)),
                           m_shadowInstances.data());
  }

  m_baseBufferDirty = false;
  m_overlayBufferDirty = false;
  m_selectionBufferDirty = false;
}

void HexViewRhiWindow::drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                                     int firstInstance, QRhiShaderResourceBindings* srb) {
  if (!buffer || count <= 0) {
    return;
  }
  if (!srb) {
    srb = m_rectSrb;
  }
  cb->setGraphicsPipeline(m_rectPso);
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

void HexViewRhiWindow::drawShadowBuffer(QRhiCommandBuffer* cb) {
  if (!m_shadowBuf || m_shadowInstances.empty() || m_view->m_shadowBlur <= 0.0f) {
    return;
  }
  cb->setGraphicsPipeline(m_shadowPso);
  cb->setShaderResources(m_shadowSrb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {m_shadowBuf, 0}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, static_cast<int>(m_shadowInstances.size()));
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

void HexViewRhiWindow::appendShadow(const QRectF& rect, float pad, const QVector4D& color) {
  const float gx = static_cast<float>(rect.x() - pad);
  const float gy = static_cast<float>(rect.y() - pad);
  const float gw = static_cast<float>(rect.width() + pad * 2.0f);
  const float gh = static_cast<float>(rect.height() + pad * 2.0f);
  m_shadowInstances.push_back({gx, gy, gw, gh,
                               static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                               static_cast<float>(rect.width()), static_cast<float>(rect.height()),
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

void HexViewRhiWindow::buildOverlayInstances(int viewportHeight) {
  m_overlayRectInstances.clear();
  if (m_view->m_overlayOpacity <= 0.0f) {
    return;
  }

  m_overlayRectInstances.reserve(2);
  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float hexStartX = static_cast<float>(m_view->hexXOffset());
  const float asciiStartX =
      hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const QVector4D overlayColor(0.0f, 0.0f, 0.0f, static_cast<float>(m_view->m_overlayOpacity));

  const float y = 0.0f;
  appendRect(m_overlayRectInstances, hexStartX - charHalfWidth, y,
             kBytesPerLine * 3.0f * charWidth, static_cast<float>(viewportHeight), overlayColor);
  if (m_view->m_shouldDrawAscii) {
    appendRect(m_overlayRectInstances, asciiStartX, y,
               kBytesPerLine * charWidth, static_cast<float>(viewportHeight), overlayColor);
  }
}

void HexViewRhiWindow::buildSelectionInstances(int startLine, int endLine) {
  m_selectionRectInstances.clear();
  m_selectionGlyphInstances.clear();
  m_shadowInstances.clear();

  const bool hasSelection = !m_view->m_selections.empty() || !m_view->m_fadeSelections.empty();
  if (!hasSelection) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount > 0) {
    m_selectionRectInstances.reserve(static_cast<size_t>(visibleCount) * 4);
    m_selectionGlyphInstances.reserve(static_cast<size_t>(visibleCount) *
                                      (m_view->m_shouldDrawAscii ? 80 : 64));
    m_shadowInstances.reserve(static_cast<size_t>(visibleCount) * 2);
  }

  const uint32_t fileLength = m_view->m_vgmfile->unLength;
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

  const bool doShadow = m_view->m_shadowBlur > 0.0f;
  const QVector4D shadowColor = toVec4(SHADOW_COLOR);
  const float shadowPadBase = m_view->m_shadowBlur * 2.0f + SELECTION_PADDING +
                              std::max(std::abs(m_view->m_shadowOffset.x()),
                                       std::abs(m_view->m_shadowOffset.y()));

  const std::vector<HexView::SelectionRange>& selections =
      m_view->m_selections.empty() ? m_view->m_fadeSelections : m_view->m_selections;

  for (const auto& selection : selections) {
    if (selection.length == 0) {
      continue;
    }
    const int selectionStart = static_cast<int>(selection.offset - m_view->m_vgmfile->dwOffset);
    const int selectionEnd = selectionStart + static_cast<int>(selection.length);
    if (selectionEnd <= 0 || selectionStart >= static_cast<int>(fileLength)) {
      continue;
    }

    const int selStartLine = std::max(startLine, selectionStart / kBytesPerLine);
    const int selEndLine = std::min(endLine, (selectionEnd - 1) / kBytesPerLine);

    for (int line = selStartLine; line <= selEndLine; ++line) {
      const CachedLine* entry = cachedLineFor(line);
      if (!entry || entry->bytes <= 0) {
        continue;
      }

      const int lineOffset = line * kBytesPerLine;
      const int overlapStart = std::max(selectionStart, lineOffset);
      const int overlapEnd = std::min(selectionEnd, lineOffset + entry->bytes);
      if (overlapEnd <= overlapStart) {
        continue;
      }

      const int startCol = overlapStart - lineOffset;
      const int length = overlapEnd - overlapStart;
      const float y = entry->line * lineHeight;

      int spanStart = startCol;
      uint16_t spanStyle = entry->styles[spanStart];
      for (int i = startCol + 1; i <= startCol + length; ++i) {
        if (i == startCol + length || entry->styles[i] != spanStyle) {
          const int spanLen = i - spanStart;
          const auto& style = styleFor(spanStyle);
          const QVector4D bgColor = toVec4(style.bg);
          const float hexX = hexStartX + spanStart * 3.0f * charWidth - charHalfWidth;
          const QRectF hexRect(hexX, y, spanLen * 3.0f * charWidth, lineHeight);
          appendRect(m_selectionRectInstances, hexRect.x(), hexRect.y(), hexRect.width(),
                     hexRect.height(), bgColor);
          if (doShadow) {
            appendShadow(hexRect, shadowPadBase, shadowColor);
          }

          if (m_view->m_shouldDrawAscii) {
            const float asciiX = asciiStartX + spanStart * charWidth;
            const QRectF asciiRect(asciiX, y, spanLen * charWidth, lineHeight);
            appendRect(m_selectionRectInstances, asciiRect.x(), asciiRect.y(), asciiRect.width(),
                       asciiRect.height(), bgColor);
            if (doShadow) {
              appendShadow(asciiRect, shadowPadBase, shadowColor);
            }
          }

          if (i < startCol + length) {
            spanStart = i;
            spanStyle = entry->styles[i];
          }
        }
      }

      for (int i = startCol; i < startCol + length; ++i) {
        const auto& style = styleFor(entry->styles[i]);
        const QVector4D textColor = toVec4(style.fg);
        const uint8_t value = entry->data[i];
        const float hexX = hexStartX + i * 3.0f * charWidth;
        appendGlyph(m_selectionGlyphInstances, hexX, y, charWidth, lineHeight,
                    hexUvs[value >> 4], textColor);
        appendGlyph(m_selectionGlyphInstances, hexX + charWidth, y, charWidth, lineHeight,
                    hexUvs[value & 0x0F], textColor);
        appendGlyph(m_selectionGlyphInstances, hexX + 2.0f * charWidth, y, charWidth, lineHeight,
                    spaceUv, textColor);

        if (m_view->m_shouldDrawAscii) {
          const char asciiChar = isPrintable(value) ? static_cast<char>(value) : '.';
          const float asciiX = asciiStartX + i * charWidth;
          appendGlyph(m_selectionGlyphInstances, asciiX, y, charWidth, lineHeight,
                      glyphUv(QLatin1Char(asciiChar)), textColor);
        }
      }
    }
  }
}
