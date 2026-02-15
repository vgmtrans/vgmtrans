/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiRenderer.h"

#include "HexView.h"
#include "LogManager.h"
#include "VGMFile.h"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QImage>
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
const QColor OUTLINE_COLOR(35, 35, 35);
constexpr float OUTLINE_ALPHA = 1.0f;
constexpr float OUTLINE_MIN_CELL_PX = 6.0f;

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

// Construct renderer state; GPU resources are created lazily in initIfNeeded().
HexViewRhiRenderer::HexViewRhiRenderer(HexView* view, const char* logLabel)
    : m_view(view), m_logLabel(logLabel) {}

// Ensure all owned QRhi resources are released before destruction.
HexViewRhiRenderer::~HexViewRhiRenderer() {
  releaseResources();
}

// Lazily create persistent GPU resources and initialize backend capabilities.
void HexViewRhiRenderer::initIfNeeded(QRhi* rhi) {
  if (m_inited || !rhi) {
    return;
  }

  m_rhi = rhi;

  if (!m_loggedInit) {
    L_DEBUG("{} init: backend={} baseInstance={}", m_logLabel, int(m_rhi->backend()),
            m_rhi->isFeatureSupported(QRhi::BaseInstance));
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

  const int screenUboSize = m_rhi->ubufAligned(kMat4Bytes + kVec4Bytes * 8);
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

// Destroy all GPU-side objects owned by the renderer and reset state flags.
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
  m_compositeSrbDirty = false;

  releaseRenderTargets();

  delete m_glyphTex;
  m_glyphTex = nullptr;
  delete m_itemIdTex;
  m_itemIdTex = nullptr;
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

// Mark base content and outline-id data as stale.
void HexViewRhiRenderer::markBaseDirty() {
  m_baseDirty = true;
  m_itemIdDirty = true;
}

// Mark selection/playback overlay geometry as stale.
void HexViewRhiRenderer::markSelectionDirty() {
  m_selectionDirty = true;
}

// Drop CPU line cache and force a full geometry/id rebuild.
void HexViewRhiRenderer::invalidateCache() {
  m_cachedLines.clear();
  m_cacheStartLine = 0;
  m_cacheEndLine = -1;
  m_baseDirty = true;
  m_selectionDirty = true;
  m_itemIdDirty = true;
}

// Render one frame from a captured HexView snapshot through the full pass chain.
void HexViewRhiRenderer::renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target) {
  if (!m_inited) {
    initIfNeeded(m_rhi);
  }
  if (!m_rhi || !cb || !m_view) {
    return;
  }
  if (!target.renderTarget || target.pixelSize.isEmpty()) {
    L_DEBUG("{} renderFrame skipped: render target pixel size empty", m_logLabel);
    return;
  }

  const float dpr = target.dpr > 0.0f ? target.dpr : 1.0f;
  const HexViewFrame::Data frame = m_view->captureRhiFrameData(dpr);
  if (!frame.vgmfile) {
    return;
  }
  const int viewportWidth = frame.viewportSize.width();
  const int viewportHeight = frame.viewportSize.height();
  if (viewportWidth <= 0 || viewportHeight <= 0 || frame.lineHeight <= 0) {
    return;
  }

  const int totalLines = frame.totalLines;
  if (totalLines <= 0) {
    return;
  }

  if (!m_loggedFrame) {
    L_DEBUG("{} frame started viewport={}x{} pixelSize={}x{}", m_logLabel, viewportWidth,
            viewportHeight, target.pixelSize.width(), target.pixelSize.height());
    m_loggedFrame = true;
  }

  const int scrollY = frame.scrollY;
  const int startLine = std::clamp(scrollY / frame.lineHeight, 0, totalLines - 1);
  const int endLine =
      std::clamp((scrollY + viewportHeight) / frame.lineHeight, 0, totalLines - 1);

  if (startLine != m_lastStartLine || endLine != m_lastEndLine) {
    m_lastStartLine = startLine;
    m_lastEndLine = endLine;
    m_selectionDirty = true;
  }

  ensureCacheWindow(startLine, endLine, totalLines, frame);

  if (m_baseDirty) {
    buildBaseInstances(frame);
    m_baseDirty = false;
    m_baseBufferDirty = true;
  }

  if (m_selectionDirty) {
    buildSelectionInstances(startLine, endLine, frame);
    m_selectionDirty = false;
    m_selectionBufferDirty = true;
  }

  QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
  if (!m_staticBuffersUploaded) {
    u->uploadStaticBuffer(m_vbuf, kVertices);
    u->uploadStaticBuffer(m_ibuf, kIndices);
    m_staticBuffersUploaded = true;
  }
  ensureGlyphTexture(u, frame);
  const float minCell = std::min<float>(frame.charWidth, frame.lineHeight);
  const bool outlineAllowed = minCell >= OUTLINE_MIN_CELL_PX;
  const float targetOutline = (outlineAllowed && frame.seekModifierActive) ? OUTLINE_ALPHA : 0.0f;
  if (std::abs(m_outlineAlpha - targetOutline) > 0.0001f) {
    m_outlineAlpha = targetOutline;
    m_itemIdDirty = true;
  }
  const bool outlineEnabled = outlineAllowed && m_outlineAlpha > 0.0f;
  if (outlineEnabled != m_outlineEnabled) {
    m_outlineEnabled = outlineEnabled;
    m_itemIdDirty = true;
  }
  ensureItemIdTexture(u, startLine, endLine, totalLines, frame);
  ensureRenderTargets(target.pixelSize);
  const int sampleCount = std::max(1, target.sampleCount);
  ensurePipelines(target.renderPassDesc, sampleCount);
  if (m_compositeSrbDirty && m_compositeSrb) {
    updateCompositeSrb();
    m_compositeSrbDirty = false;
  }
  updateUniforms(u, static_cast<float>(scrollY), target.pixelSize, frame);
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

  const QColor clearColor = frame.windowColor;

  // Pass 1: base content (background spans + glyphs) into an offscreen texture.
  cb->beginPass(m_contentRt, clearColor, {1.0f, 0}, u);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawRectBuffer(cb, m_baseRectBuf, baseRectCount, baseRectFirst);
  drawGlyphBuffer(cb, m_baseGlyphBuf, baseGlyphCount, baseGlyphFirst);
  cb->endPass();

  // Pass 2: selection/playback mask into an offscreen texture.
  // Channel encoding:
  // R=selection mask, G=active playback, B=fading playback, A=fade alpha.
  cb->beginPass(m_maskRt, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawRectBuffer(cb, m_maskRectBuf, static_cast<int>(m_maskRectInstances.size()), 0, nullptr,
                 m_maskPso);
  cb->endPass();

  // Pass 3: edge field for drop shadow and playback glow falloff.
  const bool edgeEnabled = !m_edgeRectInstances.empty() &&
      ((frame.shadowBlur > 0.0 && frame.shadowStrength > 0.0) ||
       (frame.playbackGlowRadius > 0.0f && frame.playbackGlowStrength > 0.0f));
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

  // Pass 4: fullscreen composite combines content + mask + edge + item-id textures.
  cb->beginPass(target.renderTarget, clearColor, {1.0f, 0}, nullptr);
  cb->setViewport(QRhiViewport(0, 0, target.pixelSize.width(), target.pixelSize.height()));
  drawFullscreen(cb, m_compositePso, m_compositeSrb);
  cb->endPass();
}

// Ensure offscreen content/mask/edge render targets match current output pixel size.
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

// Release offscreen textures/render-target objects for all intermediate passes.
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

// Build or rebuild pipelines and SRBs when RP descriptors/samples/bindings change.
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
  // Pipelines/SRBs are tied to render-pass descriptors and texture bindings,
  // so rebuild whenever output RP/sample count changes or internal RTs were recreated.
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
  updateCompositeSrb();

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

// Upload the glyph atlas texture when the source atlas version changes.
void HexViewRhiRenderer::ensureGlyphTexture(QRhiResourceUpdateBatch* u,
                                            const HexViewFrame::Data& frame) {
  if (!frame.glyphAtlas.image || !frame.glyphAtlas.uvTable ||
      frame.glyphAtlas.image->isNull()) {
    return;
  }

  if (m_glyphTex && m_glyphAtlasVersion == frame.glyphAtlas.version) {
    return;
  }

  delete m_glyphTex;
  m_glyphTex =
      m_rhi->newTexture(QRhiTexture::RGBA8, frame.glyphAtlas.image->size(), 1);
  m_glyphTex->create();

  u->uploadTexture(m_glyphTex, *frame.glyphAtlas.image);

  m_glyphAtlasVersion = frame.glyphAtlas.version;

  if (m_glyphSrb) {
    m_glyphSrb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                m_glyphTex, m_glyphSampler)
    });
    m_glyphSrb->create();
  }
}

// Build/update per-byte item-id texture used for shader-side outline edge detection.
void HexViewRhiRenderer::ensureItemIdTexture(QRhiResourceUpdateBatch* u, int startLine,
                                             int endLine, int totalLines,
                                             const HexViewFrame::Data& frame) {
  if (!m_rhi || !u || !frame.vgmfile) {
    return;
  }

  if (!m_outlineEnabled) {
    // Keep a tiny transparent texture bound when outlines are disabled, so the
    // composite pipeline stays valid without branching in SRB layout.
    const QSize size(1, 1);
    if (!m_itemIdTex || m_itemIdSize != size) {
      delete m_itemIdTex;
      m_itemIdTex = m_rhi->newTexture(QRhiTexture::RGBA8, size, 1);
      m_itemIdTex->create();
      m_itemIdSize = size;
      m_compositeSrbDirty = true;
    }
    if (m_itemIdDirty) {
      QImage img(1, 1, QImage::Format_RGBA8888);
      img.fill(Qt::transparent);
      u->uploadTexture(m_itemIdTex, img);
      m_itemIdDirty = false;
      m_itemIdStartLine = 0;
    }
    return;
  }

  const int padStart = std::max(0, startLine - 1);
  const int padEnd = std::min(totalLines - 1, endLine + 1);
  const QSize size(kBytesPerLine, std::max(1, padEnd - padStart + 1));
  const bool needsResize = !m_itemIdTex || m_itemIdSize != size;
  if (needsResize) {
    delete m_itemIdTex;
    m_itemIdTex = m_rhi->newTexture(QRhiTexture::RGBA8, size, 1);
    m_itemIdTex->create();
    m_itemIdSize = size;
    m_itemIdDirty = true;
    m_compositeSrbDirty = true;
  }

  if (!m_itemIdDirty && m_itemIdStartLine == padStart) {
    return;
  }

  m_itemIdStartLine = padStart;
  m_itemIdDirty = false;

  // Encode a stable 16-bit item id per byte cell (RG channels). The composite
  // shader samples neighbor ids to draw crisp inter-item outlines.
  QImage img(size, QImage::Format_RGBA8888);
  img.fill(Qt::transparent);

  std::unordered_map<const VGMItem*, uint16_t> idMap;
  idMap.reserve(static_cast<size_t>(size.width() * size.height()));
  uint16_t nextId = 1;

  const uint32_t baseOffset = frame.vgmfile->offset();
  const uint32_t endOffset = frame.vgmfile->offset() + frame.vgmfile->length();

  for (int row = 0; row < size.height(); ++row) {
    const int lineIndex = padStart + row;
    uchar* dst = img.scanLine(row);
    for (int byte = 0; byte < kBytesPerLine; ++byte) {
      const uint32_t offset = baseOffset + static_cast<uint32_t>(lineIndex * kBytesPerLine + byte);
      uint16_t id = 0;
      if (offset < endOffset) {
        if (VGMItem* item = frame.vgmfile->getItemAtOffset(offset, false)) {
          if (item->children().empty()) {
            auto [it, inserted] = idMap.emplace(item, nextId);
            if (inserted) {
              ++nextId;
            }
            id = it->second;
          }
        }
      }
      const int px = byte * 4;
      dst[px + 0] = static_cast<uchar>(id & 0xFF);
      dst[px + 1] = static_cast<uchar>((id >> 8) & 0xFF);
      dst[px + 2] = 0;
      dst[px + 3] = 255;
    }
  }

  u->uploadTexture(m_itemIdTex, img);
}

// Rebind composite pass resources after target textures or item-id texture changed.
void HexViewRhiRenderer::updateCompositeSrb() {
  if (!m_compositeSrb) {
    return;
  }
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
                                              m_edgeTex, m_glyphSampler),
    QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage,
                                              m_itemIdTex, m_maskSampler)
  });
  m_compositeSrb->create();
}

// Upload per-frame transforms and effect/shader parameters to uniform buffers.
void HexViewRhiRenderer::updateUniforms(QRhiResourceUpdateBatch* u, float scrollY,
                                        const QSize& pixelSize,
                                        const HexViewFrame::Data& frame) {
  const QSize viewSize = frame.viewportSize;
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
  const float shadowStrength =
      (frame.shadowBlur > 0.0) ? static_cast<float>(frame.shadowStrength) : 0.0f;
  const float shadowUvX =
      pixelSize.width() > 0
          ? (static_cast<float>(frame.shadowOffset.x()) * dpr / pixelSize.width())
          : 0.0f;
  const float shadowUvY =
      pixelSize.height() > 0
          ? (static_cast<float>(frame.shadowOffset.y()) * dpr / pixelSize.height())
          : 0.0f;

  const float charWidth = static_cast<float>(frame.charWidth);
  const float charHalfWidth = static_cast<float>(frame.charHalfWidth);
  const float lineHeight = static_cast<float>(frame.lineHeight);
  const float hexStartX = static_cast<float>(frame.hexStartX) - charHalfWidth;
  const float hexWidth = kBytesPerLine * 3.0f * charWidth;
  const float asciiStartX = static_cast<float>(frame.hexStartX) +
                            (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
  const float asciiWidth = frame.shouldDrawAscii ? (kBytesPerLine * charWidth) : 0.0f;

  const float shadowRadius = std::max(0.0f, static_cast<float>(frame.shadowBlur));
  const float glowRadius = std::max(0.0f, static_cast<float>(frame.playbackGlowRadius)) *
                           std::max(1.0f, std::min(charWidth, lineHeight));
  const QVector4D edgeParams(shadowRadius, glowRadius, frame.shadowEdgeCurve,
                             frame.playbackGlowEdgeCurve);

  u->updateDynamicBuffer(m_edgeUbuf, 0, kMat4Bytes, mvp.constData());
  u->updateDynamicBuffer(m_edgeUbuf, kMat4Bytes, kVec4Bytes, &edgeParams);

  const QVector4D overlayAndShadow(static_cast<float>(frame.overlayOpacity), shadowStrength,
                                   shadowUvX, shadowUvY);
  const QVector4D columnLayout(hexStartX, hexWidth, asciiStartX, asciiWidth);
  const QVector4D viewAndTime(viewW, viewH, uvFlipY, timeSeconds);
  const QVector4D shadowColor = toVec4(SHADOW_COLOR);
  const QColor glowLow = frame.playbackGlowLow;
  const QColor glowHigh = frame.playbackGlowHigh;
  const QVector4D glowLowAndStrength(glowLow.redF(), glowLow.greenF(), glowLow.blueF(),
                                     frame.playbackGlowStrength);
  const QVector4D glowHighAndDpr(glowHigh.redF(), glowHigh.greenF(), glowHigh.blueF(), dpr);
  const float outlineAlpha = m_outlineAlpha;
  const QVector4D outlineColor(OUTLINE_COLOR.redF(), OUTLINE_COLOR.greenF(),
                               OUTLINE_COLOR.blueF(), outlineAlpha);
  const QVector4D itemIdWindow(lineHeight, scrollY,
                               static_cast<float>(m_itemIdStartLine),
                               static_cast<float>(std::max(1, m_itemIdSize.height())));

  // Keep std140 slot order in sync with hexcomposite.frag's Ubuf layout.
  u->updateDynamicBuffer(m_compositeUbuf, 0, kMat4Bytes, screenMvp.constData());
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 0, kVec4Bytes,
                         &overlayAndShadow);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 1, kVec4Bytes,
                         &columnLayout);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 2, kVec4Bytes,
                         &viewAndTime);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 3, kVec4Bytes,
                         &shadowColor);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 4, kVec4Bytes,
                         &glowLowAndStrength);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 5, kVec4Bytes,
                         &glowHighAndDpr);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 6, kVec4Bytes,
                         &outlineColor);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 7, kVec4Bytes,
                         &itemIdWindow);
}

// Ensure dynamic instance buffer exists and is large enough for current upload size.
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

// Upload dirty base/mask/edge instance arrays to their corresponding dynamic buffers.
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

// Draw instanced rectangle geometry for base content or mask pass.
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

// Draw instanced edge geometry used to produce shadow/glow distance fields.
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

// Draw instanced glyph quads sampling from the glyph atlas texture.
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

// Draw a single fullscreen quad for the composite post-process pass.
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

// Resolve atlas UVs for a character, with '.' as a fallback glyph.
QRectF HexViewRhiRenderer::glyphUv(const QChar& ch,
                                   const HexViewFrame::Data& frame) const {
  if (!frame.glyphAtlas.uvTable) {
    return {};
  }
  const auto& uvTable = *frame.glyphAtlas.uvTable;
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

// Append one colored quad instance to the target rect stream.
void HexViewRhiRenderer::appendRect(std::vector<RectInstance>& rects, float x, float y, float w,
                                   float h, const QVector4D& color) {
  rects.push_back({x, y, w, h, color.x(), color.y(), color.z(), color.w()});
}

// Append one padded edge instance with both padded geom and original rect bounds.
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

// Append one glyph instance when a valid atlas UV exists.
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

// Ensure cached file-line window covers the visible range plus a scroll margin.
void HexViewRhiRenderer::ensureCacheWindow(int startLine, int endLine, int totalLines,
                                           const HexViewFrame::Data& frame) {
  const bool needsWindow =
      m_cachedLines.empty() || startLine < m_cacheStartLine || endLine > m_cacheEndLine;
  if (!needsWindow) {
    return;
  }

  // Cache extra lines around the viewport to avoid rebuilding base geometry
  // on small scroll deltas.
  const int visibleCount = endLine - startLine + 1;
  const int margin = std::max(visibleCount * 2, 1);
  m_cacheStartLine = std::max(0, startLine - margin);
  m_cacheEndLine = std::min(totalLines - 1, endLine + margin);
  rebuildCacheWindow(frame);
  m_baseDirty = true;
  m_selectionDirty = true;
}

// Rebuild cached line bytes and style ids for the current cache window.
void HexViewRhiRenderer::rebuildCacheWindow(const HexViewFrame::Data& frame) {
  m_cachedLines.clear();
  if (m_cacheEndLine < m_cacheStartLine || !frame.vgmfile) {
    return;
  }

  const uint32_t fileLength = frame.vgmfile->length();
  const auto* baseData = reinterpret_cast<const uint8_t*>(frame.vgmfile->data());
  const auto* styleIds = frame.styleIds;
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
          entry.styles[i] = (styleIds && idx >= 0 && idx < static_cast<int>(styleIds->size()))
                                ? (*styleIds)[idx]
                                : 0;
        }
      }
    }
    m_cachedLines.push_back(entry);
  }
}

// Return cached line data for a logical file line, or nullptr if unavailable.
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

// Build base background and glyph instance streams from cached file lines.
void HexViewRhiRenderer::buildBaseInstances(const HexViewFrame::Data& frame) {
  m_baseRectInstances.clear();
  m_baseGlyphInstances.clear();
  m_lineRanges.clear();

  if (m_cachedLines.empty()) {
    return;
  }

  m_baseRectInstances.reserve(m_cachedLines.size() * 4);
  m_baseGlyphInstances.reserve(m_cachedLines.size() * (frame.shouldDrawAscii ? 80 : 64));
  m_lineRanges.reserve(m_cachedLines.size());

  const QColor clearColor = frame.windowColor;
  const QVector4D addressColor = toVec4(frame.windowTextColor);

  const float charWidth = static_cast<float>(frame.charWidth);
  const float charHalfWidth = static_cast<float>(frame.charHalfWidth);
  const float lineHeight = static_cast<float>(frame.lineHeight);

  const float hexStartX = static_cast<float>(frame.hexStartX);
  const float asciiStartX =
      hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;

  const auto& styles = frame.styles;
  auto styleFor = [&](uint16_t styleId) -> const HexViewFrame::Style& {
    if (styleId < styles.size()) {
      return styles[styleId];
    }
    return styles.front();
  };

  static const char kHexDigits[] = "0123456789ABCDEF";
  std::array<QRectF, 16> hexUvs{};
  for (int i = 0; i < 16; ++i) {
    hexUvs[i] = glyphUv(QLatin1Char(kHexDigits[i]), frame);
  }
  const QRectF spaceUv = glyphUv(QLatin1Char(' '), frame);

  for (const auto& entry : m_cachedLines) {
    LineRange range{};
    range.rectStart = static_cast<uint32_t>(m_baseRectInstances.size());
    range.glyphStart = static_cast<uint32_t>(m_baseGlyphInstances.size());
    const float y = entry.line * lineHeight;

    if (frame.shouldDrawOffset) {
      uint32_t address = frame.vgmfile->offset() + (entry.line * kBytesPerLine);
      if (frame.addressAsHex) {
        char buf[NUM_ADDRESS_NIBBLES];
        for (int i = NUM_ADDRESS_NIBBLES - 1; i >= 0; --i) {
          buf[i] = kHexDigits[address & 0xF];
          address >>= 4;
        }
        for (int i = 0; i < NUM_ADDRESS_NIBBLES; ++i) {
          appendGlyph(m_baseGlyphInstances, i * charWidth, y, charWidth, lineHeight,
                      glyphUv(QLatin1Char(buf[i]), frame), addressColor);
        }
      } else {
        QString text = QString::number(address).rightJustified(NUM_ADDRESS_NIBBLES, QLatin1Char('0'));
        for (int i = 0; i < text.size(); ++i) {
          appendGlyph(m_baseGlyphInstances, i * charWidth, y, charWidth, lineHeight,
                      glyphUv(text[i], frame), addressColor);
        }
      }
    }

    if (entry.bytes <= 0) {
      continue;
    }

    int spanStart = 0;
    uint16_t spanStyle = entry.styles[0];
    // Emit one background rect per contiguous style run, instead of per byte.
    for (int i = 1; i <= entry.bytes; ++i) {
      if (i == entry.bytes || entry.styles[i] != spanStyle) {
        const int spanLen = i - spanStart;
        const auto& style = styleFor(spanStyle);
        if (style.bg != clearColor) {
          const QVector4D bgColor = toVec4(style.bg);
          const float hexX = hexStartX + spanStart * 3.0f * charWidth - charHalfWidth;
          appendRect(m_baseRectInstances, hexX, y, spanLen * 3.0f * charWidth, lineHeight, bgColor);
          if (frame.shouldDrawAscii) {
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

      if (frame.shouldDrawAscii) {
        const char asciiChar = isPrintable(value) ? static_cast<char>(value) : '.';
        const float asciiX = asciiStartX + i * charWidth;
        appendGlyph(m_baseGlyphInstances, asciiX, y, charWidth, lineHeight,
                    glyphUv(QLatin1Char(asciiChar), frame), textColor);
      }
    }

    range.rectCount = static_cast<uint32_t>(m_baseRectInstances.size()) - range.rectStart;
    range.glyphCount = static_cast<uint32_t>(m_baseGlyphInstances.size()) - range.glyphStart;
    m_lineRanges.push_back(range);
  }
}

// Convert byte-range selections into per-visible-line byte-column intervals.
void HexViewRhiRenderer::collectIntervalsForSelections(
    const std::vector<HexViewFrame::SelectionRange>& selections,
    const SelectionBuildContext& ctx,
    std::vector<std::vector<Interval>>& perLine) const {
  perLine.assign(static_cast<size_t>(ctx.visibleCount), {});

  for (const auto& selection : selections) {
    if (selection.length == 0) {
      continue;
    }

    int selectionStart = static_cast<int>(selection.offset - ctx.fileBaseOffset);
    int selectionEnd = selectionStart + static_cast<int>(selection.length);
    if (selectionEnd <= 0 || selectionStart >= static_cast<int>(ctx.fileLength)) {
      continue;
    }
    selectionStart = std::max(selectionStart, 0);
    selectionEnd = std::min(selectionEnd, static_cast<int>(ctx.fileLength));
    if (selectionEnd <= selectionStart) {
      continue;
    }

    const int selStartLine = selectionStart / kBytesPerLine;
    const int selEndLine = (selectionEnd - 1) / kBytesPerLine;
    const int lineStart = std::max(ctx.startLine, selStartLine);
    const int lineEnd = std::min(ctx.endLine, selEndLine);

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

      perLine[static_cast<size_t>(line - ctx.startLine)].push_back({clampedStart, clampedEnd});
    }
  }
}

// Sort and merge overlapping/adjacent intervals on a single line.
std::vector<HexViewRhiRenderer::Interval> HexViewRhiRenderer::mergeIntervals(
    std::vector<Interval>& intervals) {
  if (intervals.empty()) {
    return {};
  }
  std::sort(intervals.begin(), intervals.end(),
            [](const Interval& a, const Interval& b) { return a.start < b.start; });

  std::vector<Interval> merged;
  merged.reserve(intervals.size());
  for (const auto& interval : intervals) {
    if (merged.empty() || interval.start > merged.back().end) {
      merged.push_back(interval);
    } else {
      merged.back().end = std::max(merged.back().end, interval.end);
    }
  }
  return merged;
}

// Emit mask quads for merged selection intervals on one visible line.
void HexViewRhiRenderer::appendMaskRectsForIntervals(const std::vector<Interval>& intervals,
                                                     int line,
                                                     const SelectionBuildContext& ctx,
                                                     float padX,
                                                     float padY,
                                                     const QVector4D& maskColor) {
  const float y = line * ctx.lineHeight;
  for (const auto& interval : intervals) {
    const int length = interval.end - interval.start;
    if (length <= 0) {
      continue;
    }

    const float hexX = ctx.hexStartX + interval.start * 3.0f * ctx.charWidth - ctx.charHalfWidth - padX;
    appendRect(m_maskRectInstances, hexX, y - padY,
               length * 3.0f * ctx.charWidth + padX * 2.0f,
               ctx.lineHeight + padY * 2.0f, maskColor);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + interval.start * ctx.charWidth - padX;
      appendRect(m_maskRectInstances, asciiX, y - padY,
                 length * ctx.charWidth + padX * 2.0f,
                 ctx.lineHeight + padY * 2.0f, maskColor);
    }
  }
}

// Emit edge instances for finalized vertical runs of identical intervals.
void HexViewRhiRenderer::emitEdgeRuns(const std::unordered_map<uint32_t, EdgeRun>& runs,
                                      const SelectionBuildContext& ctx,
                                      float edgePad,
                                      const QVector4D& edgeColor) {
  for (const auto& [ignoredKey, run] : runs) {
    (void) ignoredKey;
    const int length = run.endCol - run.startCol;
    if (length <= 0) {
      continue;
    }

    const float runY = run.startLine * ctx.lineHeight;
    const float runH = (run.endLine - run.startLine + 1) * ctx.lineHeight;
    const float hexX = ctx.hexStartX + run.startCol * 3.0f * ctx.charWidth - ctx.charHalfWidth;
    appendEdgeRect(m_edgeRectInstances, hexX, runY, length * 3.0f * ctx.charWidth, runH,
                   edgePad, edgeColor);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + run.startCol * ctx.charWidth;
      appendEdgeRect(m_edgeRectInstances, asciiX, runY, length * ctx.charWidth, runH,
                     edgePad, edgeColor);
    }
  }
}

// Build mask and edge geometry for a selection set across the visible range.
void HexViewRhiRenderer::appendMaskForSelections(
    const std::vector<HexViewFrame::SelectionRange>& selections,
                                                 const SelectionBuildContext& ctx,
                                                 float padX,
                                                 float padY,
                                                 float edgePad,
                                                 const QVector4D& maskColor,
                                                 const QVector4D& edgeColor) {
  std::vector<std::vector<Interval>> perLine;
  collectIntervalsForSelections(selections, ctx, perLine);

  std::unordered_map<uint32_t, EdgeRun> activeRuns;
  for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
    auto& intervals = perLine[static_cast<size_t>(line - ctx.startLine)];
    const std::vector<Interval> merged = mergeIntervals(intervals);
    appendMaskRectsForIntervals(merged, line, ctx, padX, padY, maskColor);

    if (edgePad <= 0.0f) {
      continue;
    }

    std::unordered_map<uint32_t, EdgeRun> nextRuns;
    // Coalesce identical intervals across adjacent lines into vertical runs.
    // This keeps edge geometry compact when selections span many rows.
    for (const auto& interval : merged) {
      const uint32_t key = (static_cast<uint32_t>(interval.start) << 16) | static_cast<uint32_t>(interval.end);
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

    emitEdgeRuns(activeRuns, ctx, edgePad, edgeColor);
    activeRuns = std::move(nextRuns);
  }

  if (edgePad > 0.0f) {
    emitEdgeRuns(activeRuns, ctx, edgePad, edgeColor);
  }
}

// Build all selection/playback mask and edge instances for the current viewport.
void HexViewRhiRenderer::buildSelectionInstances(int startLine, int endLine,
                                                 const HexViewFrame::Data& frame) {
  m_maskRectInstances.clear();
  m_edgeRectInstances.clear();

  const bool hasSelection = !frame.selections.empty() || !frame.fadeSelections.empty();
  const bool hasPlayback = !frame.playbackSelections.empty() ||
                           !frame.fadePlaybackSelections.empty();
  if ((!hasSelection && !hasPlayback) || startLine > endLine) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount <= 0) {
    return;
  }

  m_maskRectInstances.reserve(static_cast<size_t>(visibleCount) * (frame.shouldDrawAscii ? 8 : 4));
  m_edgeRectInstances.reserve(static_cast<size_t>(visibleCount) * (frame.shouldDrawAscii ? 8 : 4));

  const float charWidth = static_cast<float>(frame.charWidth);
  const float lineHeight = static_cast<float>(frame.lineHeight);
  SelectionBuildContext ctx;
  ctx.startLine = startLine;
  ctx.endLine = endLine;
  ctx.visibleCount = visibleCount;
  ctx.fileBaseOffset = frame.vgmfile ? frame.vgmfile->offset() : 0;
  ctx.fileLength = frame.vgmfile ? frame.vgmfile->length() : 0;
  ctx.charWidth = charWidth;
  ctx.charHalfWidth = static_cast<float>(frame.charHalfWidth);
  ctx.lineHeight = lineHeight;
  ctx.hexStartX = static_cast<float>(frame.hexStartX);
  ctx.asciiStartX = ctx.hexStartX + (kBytesPerLine * 3 + HEX_TO_ASCII_SPACING_CHARS) * ctx.charWidth;
  ctx.shouldDrawAscii = frame.shouldDrawAscii;

  const QVector4D selectionMaskColor(1.0f, 0.0f, 0.0f, 0.0f);
  const QVector4D selectionEdgeColor(1.0f, 0.0f, 0.0f, 1.0f);
  const float shadowPad = std::max(0.0f, static_cast<float>(frame.shadowBlur));
  const float glowBase = std::max(0.0f, static_cast<float>(frame.playbackGlowRadius)) *
                         std::max(1.0f, std::min(charWidth, lineHeight));
  const float glowPad = glowBase * 1.35f;

  if (hasSelection) {
    const std::vector<HexViewFrame::SelectionRange>& selections =
        frame.selections.empty() ? frame.fadeSelections : frame.selections;
    appendMaskForSelections(selections, ctx, 0.0f, 0.0f, shadowPad, selectionMaskColor,
                            selectionEdgeColor);
  }

  if (!frame.playbackSelections.empty()) {
    // Active playback in green channel.
    const QVector4D playbackMaskColor(0.0f, 1.0f, 0.0f, 0.0f);
    const QVector4D playbackEdgeColor(0.0f, 1.0f, 0.0f, 0.0f);
    appendMaskForSelections(frame.playbackSelections, ctx, 0.0f, 0.0f, glowPad,
                            playbackMaskColor, playbackEdgeColor);
  }

  if (!frame.fadePlaybackSelections.empty()) {
    // Fading playback uses blue for mask membership and alpha as fade amount.
    std::vector<HexViewFrame::SelectionRange> fadeRanges;
    fadeRanges.reserve(frame.fadePlaybackSelections.size());
    for (const auto& selection : frame.fadePlaybackSelections) {
      fadeRanges.push_back(selection.range);
    }

    const QVector4D playbackMaskColor(0.0f, 0.0f, 1.0f, 0.0f);
    const QVector4D playbackEdgeColor(0.0f, 0.0f, 1.0f, 0.0f);
    appendMaskForSelections(fadeRanges, ctx, 0.0f, 0.0f, glowPad, playbackMaskColor,
                            playbackEdgeColor);
    const QVector4D fadeEdgeColor(0.0f, 0.0f, 0.0f, 0.0f);
    for (const auto& selection : frame.fadePlaybackSelections) {
      if (selection.alpha <= 0.0f) {
        continue;
      }
      const QVector4D fadeMaskColor(0.0f, 0.0f, 1.0f, selection.alpha);
      const std::vector<HexViewFrame::SelectionRange> one{selection.range};
      appendMaskForSelections(one, ctx, 0.0f, 0.0f, 0.0f, fadeMaskColor, fadeEdgeColor);
    }
  }
}
