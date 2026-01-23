/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiRenderer.h"

#include "HexView.h"
#include "VGMFile.h"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QMatrix4x4>
#include <QPalette>
#include <QRectF>
#include <QScrollBar>
#include <QString>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>

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

bool isRhiDebugEnabled() {
  // return qEnvironmentVariableIsSet("VGMTRANS_HEXVIEW_RHI_DEBUG");
  return true;
}
}  // namespace

HexViewRhiRenderer::HexViewRhiRenderer(HexView* view, const char* logLabel)
    : m_view(view), m_logLabel(logLabel) {}

HexViewRhiRenderer::~HexViewRhiRenderer() {
  releaseResources();
}

void HexViewRhiRenderer::initIfNeeded(QRhi* rhi) {
  if (m_inited || !rhi) {
    return;
  }

  m_rhi = rhi;

  const bool debug = debugLoggingEnabled();
  if (debug && !m_loggedInit) {
    qDebug() << m_logLabel << "init:"
             << "backend=" << int(m_rhi->backend())
             << "baseInstance=" << m_rhi->isFeatureSupported(QRhi::BaseInstance);
    m_loggedInit = true;
  }
  m_supportsBaseInstance = m_rhi->isFeatureSupported(QRhi::BaseInstance);

  m_vbuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kVertices));
  m_vbuf->create();
  m_ibuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, sizeof(kIndices));
  m_ibuf->create();

  const int baseUboSize = m_rhi->ubufAligned(kMat4Bytes);
  m_ubuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, baseUboSize);
  m_ubuf->create();

  const int edgeUboSize = m_rhi->ubufAligned(kMat4Bytes + kVec4Bytes);
  m_edgeUbuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, edgeUboSize);
  m_edgeUbuf->create();

  const int screenUboSize = m_rhi->ubufAligned(kMat4Bytes + kVec4Bytes * 6);
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
  if (!m_animTimer.isValid()) {
    m_animTimer.start();
  }
  m_inited = true;
}

void HexViewRhiRenderer::releaseResources() {
  if (m_rhi) {
    m_rhi->makeThreadLocalNativeContextCurrent();
  }

  delete m_rectPso;
  m_rectPso = nullptr;
  delete m_glyphPso;
  m_glyphPso = nullptr;
  delete m_maskPso;
  m_maskPso = nullptr;
  delete m_edgePso;
  m_edgePso = nullptr;
  delete m_compositePso;
  m_compositePso = nullptr;

  delete m_rectSrb;
  m_rectSrb = nullptr;
  delete m_glyphSrb;
  m_glyphSrb = nullptr;
  delete m_edgeSrb;
  m_edgeSrb = nullptr;
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
  delete m_edgeRectBuf;
  m_edgeRectBuf = nullptr;
  delete m_ubuf;
  m_ubuf = nullptr;
  delete m_edgeUbuf;
  m_edgeUbuf = nullptr;
  delete m_compositeUbuf;
  m_compositeUbuf = nullptr;

  m_pipelinesDirty = true;
  m_inited = false;
  m_rhi = nullptr;
  m_outputRp = nullptr;
}

void HexViewRhiRenderer::markBaseDirty() {
  m_baseDirty = true;
}

void HexViewRhiRenderer::markSelectionDirty() {
  m_selectionDirty = true;
}

void HexViewRhiRenderer::invalidateCache() {
  m_cachedLines.clear();
  m_cacheStartLine = 0;
  m_cacheEndLine = -1;
  m_baseDirty = true;
  m_selectionDirty = true;
}

void HexViewRhiRenderer::renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target) {
  if (!m_inited) {
    initIfNeeded(m_rhi);
  }
  if (!m_rhi || !cb || !m_view || !m_view->viewport() || !m_view->m_vgmfile) {
    return;
  }
  if (!target.renderTarget || target.pixelSize.isEmpty()) {
    if (debugLoggingEnabled()) {
      qDebug() << m_logLabel << "renderFrame skipped: render target pixel size empty";
    }
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

  if (debugLoggingEnabled() && !m_loggedFrame) {
    qDebug() << m_logLabel << "frame started"
             << "viewport=" << QSize(viewportWidth, viewportHeight)
             << "pixelSize=" << target.pixelSize;
    m_loggedFrame = true;
  }

  const int scrollY = m_view->verticalScrollBar()->value();
  const int startLine = std::clamp(scrollY / m_view->m_lineHeight, 0, totalLines - 1);
  const int endLine =
      std::clamp((scrollY + viewportHeight) / m_view->m_lineHeight, 0, totalLines - 1);

  if (startLine != m_lastStartLine || endLine != m_lastEndLine) {
    m_lastStartLine = startLine;
    m_lastEndLine = endLine;
    m_selectionDirty = true;
  }

  const float dpr = target.dpr > 0.0f ? target.dpr : 1.0f;
  m_view->ensureGlyphAtlas(dpr);
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
  ensureRenderTargets(target.pixelSize);
  const int sampleCount = std::max(1, target.sampleCount);
  ensurePipelines(target.renderPassDesc, sampleCount);
  updateUniforms(u, static_cast<float>(scrollY), target.pixelSize);
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

  cb->beginPass(m_contentRt, clearColor, {1.0f, 0}, u);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawRectBuffer(cb, m_baseRectBuf, baseRectCount, baseRectFirst);
  drawGlyphBuffer(cb, m_baseGlyphBuf, baseGlyphCount, baseGlyphFirst);
  cb->endPass();

  cb->beginPass(m_maskRt, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawRectBuffer(cb, m_maskRectBuf, static_cast<int>(m_maskRectInstances.size()), 0, nullptr,
                 m_maskPso);
  cb->endPass();

  const bool edgeEnabled = !m_edgeRectInstances.empty() &&
      ((m_view->m_shadowBlur > 0.0f && m_view->m_shadowStrength > 0.0f) ||
       (m_view->m_playbackGlowRadius > 0.0f && m_view->m_playbackGlowStrength > 0.0f));
  if (edgeEnabled) {
    cb->beginPass(m_edgeRt, QColor(255, 255, 255, 255), {1.0f, 0}, nullptr);
    cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
    drawEdgeBuffer(cb, m_edgeRectBuf, static_cast<int>(m_edgeRectInstances.size()), 0,
                   m_edgeSrb, m_edgePso);
    cb->endPass();
  } else {
    cb->beginPass(m_edgeRt, QColor(255, 255, 255, 255), {1.0f, 0}, nullptr);
    cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
    cb->endPass();
  }

  cb->beginPass(target.renderTarget, clearColor, {1.0f, 0}, nullptr);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawFullscreen(cb, m_compositePso, m_compositeSrb);
  cb->endPass();
}

void HexViewRhiRenderer::ensureRenderTargets(const QSize& pixelSize) {
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
  makeTarget(&m_edgeTex, &m_edgeRt, &m_edgeRp);

  m_pipelinesDirty = true;
}

void HexViewRhiRenderer::releaseRenderTargets() {
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

  delete m_edgeRp;
  m_edgeRp = nullptr;
  delete m_edgeRt;
  m_edgeRt = nullptr;
  delete m_edgeTex;
  m_edgeTex = nullptr;
}

bool HexViewRhiRenderer::debugLoggingEnabled() const {
  return isRhiDebugEnabled();
}

void HexViewRhiRenderer::ensurePipelines(QRhiRenderPassDescriptor* outputRp,
                                         int outputSampleCount) {
  if (!outputRp || !m_contentRp || !m_maskRp || !m_edgeRp) {
    return;
  }

  if (outputRp != m_outputRp) {
    m_outputRp = outputRp;
    m_pipelinesDirty = true;
  }

  if (!m_pipelinesDirty && m_rectPso && m_sampleCount == outputSampleCount) {
    return;
  }
  m_sampleCount = outputSampleCount;
  m_pipelinesDirty = false;

  delete m_rectPso;
  delete m_glyphPso;
  delete m_maskPso;
  delete m_edgePso;
  delete m_compositePso;
  delete m_rectSrb;
  delete m_glyphSrb;
  delete m_edgeSrb;
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

  m_edgeSrb = m_rhi->newShaderResourceBindings();
  m_edgeSrb->setBindings({
    QRhiShaderResourceBinding::uniformBuffer(0,
                                             QRhiShaderResourceBinding::VertexStage |
                                             QRhiShaderResourceBinding::FragmentStage,
                                             m_edgeUbuf)
  });
  m_edgeSrb->create();

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
                                              m_edgeTex, m_glyphSampler)
  });
  m_compositeSrb->create();

  QShader rectVert = loadShader(":/shaders/hexquad.vert.qsb");
  QShader rectFrag = loadShader(":/shaders/hexquad.frag.qsb");
  QShader glyphVert = loadShader(":/shaders/hexglyph.vert.qsb");
  QShader glyphFrag = loadShader(":/shaders/hexglyph.frag.qsb");
  QShader screenVert = loadShader(":/shaders/hexscreen.vert.qsb");
  QShader edgeVert = loadShader(":/shaders/hexedge.vert.qsb");
  QShader edgeFrag = loadShader(":/shaders/hexedge.frag.qsb");
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

  QRhiVertexInputLayout edgeInputLayout;
  edgeInputLayout.setBindings({
    {2 * sizeof(float)},
    {sizeof(EdgeInstance), QRhiVertexInputBinding::PerInstance}
  });
  edgeInputLayout.setAttributes({
    {0, 0, QRhiVertexInputAttribute::Float2, 0},
    {1, 1, QRhiVertexInputAttribute::Float4, 0},
    {1, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)},
    {1, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)}
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

  QRhiGraphicsPipeline::TargetBlend maskBlend;
  maskBlend.enable = true;
  maskBlend.srcColor = QRhiGraphicsPipeline::One;
  maskBlend.dstColor = QRhiGraphicsPipeline::One;
  maskBlend.opColor = QRhiGraphicsPipeline::Max;
  maskBlend.srcAlpha = QRhiGraphicsPipeline::One;
  maskBlend.dstAlpha = QRhiGraphicsPipeline::One;
  maskBlend.opAlpha = QRhiGraphicsPipeline::Max;

  QRhiGraphicsPipeline::TargetBlend edgeBlend;
  edgeBlend.enable = true;
  edgeBlend.srcColor = QRhiGraphicsPipeline::One;
  edgeBlend.dstColor = QRhiGraphicsPipeline::One;
  edgeBlend.opColor = QRhiGraphicsPipeline::Min;
  edgeBlend.srcAlpha = QRhiGraphicsPipeline::One;
  edgeBlend.dstAlpha = QRhiGraphicsPipeline::One;
  edgeBlend.opAlpha = QRhiGraphicsPipeline::Min;

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
  m_maskPso->setTargetBlends({maskBlend});
  m_maskPso->setSampleCount(1);
  m_maskPso->setRenderPassDescriptor(m_maskRp);
  m_maskPso->create();

  m_edgePso = m_rhi->newGraphicsPipeline();
  m_edgePso->setShaderStages({{QRhiShaderStage::Vertex, edgeVert},
                              {QRhiShaderStage::Fragment, edgeFrag}});
  m_edgePso->setVertexInputLayout(edgeInputLayout);
  m_edgePso->setShaderResourceBindings(m_edgeSrb);
  m_edgePso->setCullMode(QRhiGraphicsPipeline::None);
  m_edgePso->setTargetBlends({edgeBlend});
  m_edgePso->setSampleCount(1);
  m_edgePso->setRenderPassDescriptor(m_edgeRp);
  m_edgePso->create();

  m_compositePso = m_rhi->newGraphicsPipeline();
  m_compositePso->setShaderStages({{QRhiShaderStage::Vertex, screenVert},
                                   {QRhiShaderStage::Fragment, compositeFrag}});
  m_compositePso->setVertexInputLayout(screenInputLayout);
  m_compositePso->setShaderResourceBindings(m_compositeSrb);
  m_compositePso->setCullMode(QRhiGraphicsPipeline::None);
  m_compositePso->setSampleCount(m_sampleCount);
  m_compositePso->setRenderPassDescriptor(m_outputRp);
  m_compositePso->create();
}

void HexViewRhiRenderer::ensureGlyphTexture(QRhiResourceUpdateBatch* u) {
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

void HexViewRhiRenderer::updateUniforms(QRhiResourceUpdateBatch* u, float scrollY,
                                       const QSize& pixelSize) {
  const QSize viewSize = m_view->viewport()->size();
  const float uvFlipY = m_rhi->isYUpInFramebuffer() ? 0.0f : 1.0f;
  const float timeSeconds = m_animTimer.isValid() ? (m_animTimer.elapsed() / 1000.0f) : 0.0f;

  QMatrix4x4 proj;
  proj.ortho(0.f, float(viewSize.width()), float(viewSize.height()), 0.f, -1.f, 1.f);

  QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix() * proj;
  mvp.translate(0.f, -scrollY, 0.f);

  u->updateDynamicBuffer(m_ubuf, 0, kMat4Bytes, mvp.constData());

  QMatrix4x4 screenMvp = m_rhi->clipSpaceCorrMatrix();
  const float dpr = viewSize.width() > 0 ? (static_cast<float>(pixelSize.width()) / viewSize.width()) : 1.0f;

  const float viewW = static_cast<float>(viewSize.width());
  const float viewH = static_cast<float>(viewSize.height());
  const float shadowStrength = (m_view->m_shadowBlur > 0.0f) ? float(m_view->m_shadowStrength) : 0.0f;
  const float shadowUvX = pixelSize.width() > 0 ? (m_view->m_shadowOffset.x() * dpr / pixelSize.width()) : 0.0f;
  const float shadowUvY = pixelSize.height() > 0 ? (m_view->m_shadowOffset.y() * dpr / pixelSize.height()) : 0.0f;

  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float lineHeight = static_cast<float>(m_view->m_lineHeight);
  const float hexStartX = static_cast<float>(m_view->hexXOffset()) - charHalfWidth;
  const float hexWidth = kBytesPerLine * 3.0f * charWidth;
  const float asciiStartX =
      static_cast<float>(m_view->hexXOffset()) +
      (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const float asciiWidth =
      m_view->m_shouldDrawAscii ? (kBytesPerLine * charWidth) : 0.0f;

  const float shadowRadius = std::max(0.0f, static_cast<float>(m_view->m_shadowBlur));
  const float glowRadius = std::max(0.0f, static_cast<float>(m_view->m_playbackGlowRadius)) *
                           std::max(1.0f, std::min(charWidth, lineHeight));
  const QVector4D edgeParams(shadowRadius, glowRadius, m_view->m_shadowEdgeCurve,
                             m_view->m_playbackGlowEdgeCurve);

  u->updateDynamicBuffer(m_edgeUbuf, 0, kMat4Bytes, mvp.constData());
  u->updateDynamicBuffer(m_edgeUbuf, kMat4Bytes, kVec4Bytes, &edgeParams);

  const QVector4D p0(static_cast<float>(m_view->m_overlayOpacity), shadowStrength,
                     shadowUvX, shadowUvY);
  const QVector4D p1(hexStartX, hexWidth, asciiStartX, asciiWidth);
  const QVector4D p2(viewW, viewH, uvFlipY, timeSeconds);
  const QVector4D p3 = toVec4(SHADOW_COLOR);
  const QColor glowLow = m_view->m_playbackGlowLow;
  const QColor glowHigh = m_view->m_playbackGlowHigh;
  const QVector4D p4(glowLow.redF(), glowLow.greenF(), glowLow.blueF(),
                     static_cast<float>(m_view->m_playbackGlowStrength));
  const QVector4D p5(glowHigh.redF(), glowHigh.greenF(), glowHigh.blueF(), 0.0f);

  u->updateDynamicBuffer(m_compositeUbuf, 0, kMat4Bytes, screenMvp.constData());
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 0, kVec4Bytes, &p0);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 1, kVec4Bytes, &p1);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 2, kVec4Bytes, &p2);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 3, kVec4Bytes, &p3);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 4, kVec4Bytes, &p4);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 5, kVec4Bytes, &p5);
}

bool HexViewRhiRenderer::ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes) {
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

void HexViewRhiRenderer::updateInstanceBuffers(QRhiResourceUpdateBatch* u) {
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

  bool edgeRectResized = ensureInstanceBuffer(
      m_edgeRectBuf, static_cast<int>(m_edgeRectInstances.size() * sizeof(EdgeInstance)));
  if ((m_selectionBufferDirty || edgeRectResized) && !m_edgeRectInstances.empty()) {
    u->updateDynamicBuffer(m_edgeRectBuf, 0,
                           static_cast<int>(m_edgeRectInstances.size() * sizeof(EdgeInstance)),
                           m_edgeRectInstances.data());
  }

  m_baseBufferDirty = false;
  m_selectionBufferDirty = false;
}

void HexViewRhiRenderer::drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
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
  quint32 instanceOffset = 0;
  int drawFirstInstance = firstInstance;
  if (!m_supportsBaseInstance && firstInstance > 0) {
    instanceOffset = static_cast<quint32>(firstInstance * sizeof(RectInstance));
    drawFirstInstance = 0;
  }
  cb->setGraphicsPipeline(pso);
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {buffer, instanceOffset}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, count, 0, 0, drawFirstInstance);
}

void HexViewRhiRenderer::drawEdgeBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                                       int firstInstance, QRhiShaderResourceBindings* srb,
                                       QRhiGraphicsPipeline* pso) {
  if (!buffer || count <= 0) {
    return;
  }
  if (!srb) {
    srb = m_edgeSrb;
  }
  if (!pso) {
    pso = m_edgePso;
  }
  quint32 instanceOffset = 0;
  int drawFirstInstance = firstInstance;
  if (!m_supportsBaseInstance && firstInstance > 0) {
    instanceOffset = static_cast<quint32>(firstInstance * sizeof(EdgeInstance));
    drawFirstInstance = 0;
  }
  cb->setGraphicsPipeline(pso);
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {buffer, instanceOffset}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, count, 0, 0, drawFirstInstance);
}
void HexViewRhiRenderer::drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                                        int firstInstance) {
  if (!buffer || count <= 0) {
    return;
  }
  quint32 instanceOffset = 0;
  int drawFirstInstance = firstInstance;
  if (!m_supportsBaseInstance && firstInstance > 0) {
    instanceOffset = static_cast<quint32>(firstInstance * sizeof(GlyphInstance));
    drawFirstInstance = 0;
  }
  cb->setGraphicsPipeline(m_glyphPso);
  cb->setShaderResources(m_glyphSrb);
  const QRhiCommandBuffer::VertexInput vbufBindings[] = {
    {m_vbuf, 0},
    {buffer, instanceOffset}
  };
  cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
  cb->drawIndexed(6, count, 0, 0, drawFirstInstance);
}

void HexViewRhiRenderer::drawFullscreen(QRhiCommandBuffer* cb, QRhiGraphicsPipeline* pso,
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

QRectF HexViewRhiRenderer::glyphUv(const QChar& ch) const {
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

void HexViewRhiRenderer::appendRect(std::vector<RectInstance>& rects, float x, float y, float w,
                                   float h, const QVector4D& color) {
  rects.push_back({x, y, w, h, color.x(), color.y(), color.z(), color.w()});
}

void HexViewRhiRenderer::appendEdgeRect(std::vector<EdgeInstance>& rects, float x, float y, float w,
                                       float h, float pad, const QVector4D& color) {
  const float geomX = x - pad;
  const float geomY = y - pad;
  const float geomW = w + pad * 2.0f;
  const float geomH = h + pad * 2.0f;
  rects.push_back({geomX, geomY, geomW, geomH,
                   x, y, w, h,
                   color.x(), color.y(), color.z(), color.w()});
}

void HexViewRhiRenderer::appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w,
                                    float h, const QRectF& uv, const QVector4D& color) {
  if (uv.isNull()) {
    return;
  }
  glyphs.push_back({x, y, w, h,
                    static_cast<float>(uv.left()), static_cast<float>(uv.top()),
                    static_cast<float>(uv.right()), static_cast<float>(uv.bottom()),
                    color.x(), color.y(), color.z(), color.w()});
}

void HexViewRhiRenderer::ensureCacheWindow(int startLine, int endLine, int totalLines) {
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

void HexViewRhiRenderer::rebuildCacheWindow() {
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

const HexViewRhiRenderer::CachedLine* HexViewRhiRenderer::cachedLineFor(int line) const {
  if (line < m_cacheStartLine || line > m_cacheEndLine) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(line - m_cacheStartLine);
  if (index >= m_cachedLines.size()) {
    return nullptr;
  }
  return &m_cachedLines[index];
}

void HexViewRhiRenderer::buildBaseInstances() {
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

void HexViewRhiRenderer::buildSelectionInstances(int startLine, int endLine) {
  m_maskRectInstances.clear();
  m_edgeRectInstances.clear();

  const bool hasSelection = !m_view->m_selections.empty() || !m_view->m_fadeSelections.empty();
  const bool hasPlayback = !m_view->m_playbackSelections.empty() ||
                           !m_view->m_fadePlaybackSelections.empty();
  if ((!hasSelection && !hasPlayback) || startLine > endLine) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount <= 0) {
    return;
  }

  m_maskRectInstances.reserve(static_cast<size_t>(visibleCount) *
                              (m_view->m_shouldDrawAscii ? 8 : 4));
  m_edgeRectInstances.reserve(static_cast<size_t>(visibleCount) *
                              (m_view->m_shouldDrawAscii ? 8 : 4));

  const uint32_t fileLength = m_view->m_vgmfile->unLength;
  const float charWidth = static_cast<float>(m_view->m_charWidth);
  const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
  const float lineHeight = static_cast<float>(m_view->m_lineHeight);
  const float hexStartX = static_cast<float>(m_view->hexXOffset());
  const float asciiStartX =
      hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const QVector4D selectionMaskColor(1.0f, 0.0f, 0.0f, 0.0f);
  const QVector4D selectionEdgeColor(1.0f, 0.0f, 0.0f, 1.0f);
  const float shadowPad = std::max(0.0f, static_cast<float>(m_view->m_shadowBlur));
  const float glowBase = std::max(0.0f, static_cast<float>(m_view->m_playbackGlowRadius)) *
                         std::max(1.0f, std::min(charWidth, lineHeight));
  const float glowPad = glowBase * 1.35f;

  struct Interval {
    int start = 0;
    int end = 0;
  };

  struct EdgeRun {
    int startCol = 0;
    int endCol = 0;
    int startLine = 0;
    int endLine = 0;
  };

  auto appendMaskForSelections = [&](const auto& selections,
                                     float padX,
                                     float padY,
                                     float edgePad,
                                     const QVector4D& maskColor,
                                     const QVector4D& edgeColor) {
    std::vector<std::vector<Interval>> perLine;
    perLine.resize(static_cast<size_t>(visibleCount));

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

    std::unordered_map<uint32_t, EdgeRun> activeRuns;

    for (int line = startLine; line <= endLine; ++line) {
      auto& intervals = perLine[static_cast<size_t>(line - startLine)];
      if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end(),
                  [](const Interval& a, const Interval& b) { return a.start < b.start; });
      }

      std::vector<Interval> merged;
      merged.reserve(intervals.size());
      for (const auto& interval : intervals) {
        if (merged.empty() || interval.start > merged.back().end) {
          merged.push_back(interval);
        } else {
          merged.back().end = std::max(merged.back().end, interval.end);
        }
      }

      const float y = line * lineHeight;
      for (const auto& interval : merged) {
        const int length = interval.end - interval.start;
        if (length <= 0) {
          continue;
        }
        const float hexX = hexStartX + interval.start * 3.0f * charWidth - charHalfWidth - padX;
        appendRect(m_maskRectInstances, hexX, y - padY,
                   length * 3.0f * charWidth + padX * 2.0f, lineHeight + padY * 2.0f, maskColor);
        if (m_view->m_shouldDrawAscii) {
          const float asciiX = asciiStartX + interval.start * charWidth - padX;
          appendRect(m_maskRectInstances, asciiX, y - padY,
                     length * charWidth + padX * 2.0f, lineHeight + padY * 2.0f, maskColor);
        }
      }

      std::unordered_map<uint32_t, EdgeRun> nextRuns;
      if (edgePad > 0.0f) {
        for (const auto& interval : merged) {
          const uint32_t key = (static_cast<uint32_t>(interval.start) << 16) |
                               static_cast<uint32_t>(interval.end);
          auto it = activeRuns.find(key);
          if (it != activeRuns.end()) {
            EdgeRun run = it->second;
            run.endLine = line;
            nextRuns.emplace(key, run);
            activeRuns.erase(it);
          } else {
            nextRuns.emplace(key, EdgeRun{interval.start, interval.end, line, line});
          }
        }

        auto emitRun = [&](const EdgeRun& run) {
          const int length = run.endCol - run.startCol;
          if (length <= 0) {
            return;
          }
          const float runY = run.startLine * lineHeight;
          const float runH = (run.endLine - run.startLine + 1) * lineHeight;
          const float hexX = hexStartX + run.startCol * 3.0f * charWidth - charHalfWidth;
          appendEdgeRect(m_edgeRectInstances, hexX, runY,
                         length * 3.0f * charWidth, runH,
                         edgePad, edgeColor);
          if (m_view->m_shouldDrawAscii) {
            const float asciiX = asciiStartX + run.startCol * charWidth;
            appendEdgeRect(m_edgeRectInstances, asciiX, runY,
                           length * charWidth, runH,
                           edgePad, edgeColor);
          }
        };

        for (const auto& [key, run] : activeRuns) {
          emitRun(run);
        }

        activeRuns = std::move(nextRuns);
      }
    }

    if (edgePad > 0.0f) {
      auto emitRun = [&](const EdgeRun& run) {
        const int length = run.endCol - run.startCol;
        if (length <= 0) {
          return;
        }
        const float runY = run.startLine * lineHeight;
        const float runH = (run.endLine - run.startLine + 1) * lineHeight;
        const float hexX = hexStartX + run.startCol * 3.0f * charWidth - charHalfWidth;
        appendEdgeRect(m_edgeRectInstances, hexX, runY,
                       length * 3.0f * charWidth, runH,
                       edgePad, edgeColor);
        if (m_view->m_shouldDrawAscii) {
          const float asciiX = asciiStartX + run.startCol * charWidth;
          appendEdgeRect(m_edgeRectInstances, asciiX, runY,
                         length * charWidth, runH,
                         edgePad, edgeColor);
        }
      };

      for (const auto& [key, run] : activeRuns) {
        emitRun(run);
      }
    }
  };

  if (hasSelection) {
    const std::vector<HexView::SelectionRange>& selections =
        m_view->m_selections.empty() ? m_view->m_fadeSelections : m_view->m_selections;
    appendMaskForSelections(selections, 0.0f, 0.0f, shadowPad,
                            selectionMaskColor, selectionEdgeColor);
  }

  if (!m_view->m_playbackSelections.empty()) {
    const QVector4D playbackMaskColor(0.0f, 1.0f, 0.0f, 0.0f);
    const QVector4D playbackEdgeColor(0.0f, 1.0f, 0.0f, 0.0f);
    appendMaskForSelections(m_view->m_playbackSelections, 0.0f, 0.0f, glowPad,
                            playbackMaskColor, playbackEdgeColor);
  }

  if (!m_view->m_fadePlaybackSelections.empty()) {
    std::vector<HexView::SelectionRange> fadeRanges;
    fadeRanges.reserve(m_view->m_fadePlaybackSelections.size());
    for (const auto& selection : m_view->m_fadePlaybackSelections) {
      fadeRanges.push_back(selection.range);
    }

    const QVector4D playbackMaskColor(0.0f, 0.0f, 1.0f, 0.0f);
    const QVector4D playbackEdgeColor(0.0f, 0.0f, 1.0f, 0.0f);
    appendMaskForSelections(fadeRanges, 0.0f, 0.0f, glowPad,
                            playbackMaskColor, playbackEdgeColor);
    const QVector4D fadeEdgeColor(0.0f, 0.0f, 0.0f, 0.0f);
    for (const auto& selection : m_view->m_fadePlaybackSelections) {
      if (selection.alpha <= 0.0f) {
        continue;
      }
      const QVector4D fadeMaskColor(0.0f, 0.0f, 1.0f, selection.alpha);
      std::array<HexView::SelectionRange, 1> one{selection.range};
      appendMaskForSelections(one, 0.0f, 0.0f, 0.0f, fadeMaskColor, fadeEdgeColor);
    }
  }
}
