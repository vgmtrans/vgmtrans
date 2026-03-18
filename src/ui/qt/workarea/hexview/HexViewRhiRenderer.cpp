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
#include <QRectF>
#include <QString>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace {
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int BYTES_PER_LINE = 16;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
const QColor OUTLINE_COLOR(35, 35, 35);
constexpr float OUTLINE_ALPHA = 1.0f;
constexpr float OUTLINE_MIN_CELL_PX = 6.0f;
constexpr float OUTLINE_FADE_SPEED = (OUTLINE_FADE_DURATION_MS > 0)
        ? (1000.0f / static_cast<float>(OUTLINE_FADE_DURATION_MS))
        : 1000.0f;

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

float pxToLogical(int px, float dpr) {
  return dpr > 0.0f ? (static_cast<float>(px) / dpr) : static_cast<float>(px);
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

HexViewRhiRenderer::LayoutMetrics HexViewRhiRenderer::computeLayoutMetrics(
    const HexViewFrame::Data& frame) {
  LayoutMetrics layout;
  layout.dpr = frame.dpr > 0.0 ? static_cast<float>(frame.dpr) : 1.0f;
  const int charWidthPx =
      std::max(1, static_cast<int>(std::round(static_cast<float>(frame.charWidth) * layout.dpr)));
  const int charHalfWidthPx = charWidthPx / 2;
  const int hexStartXPx = frame.shouldDrawOffset
                              ? ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidthPx)
                              : charHalfWidthPx;
  const int hexGlyphStartXPx = hexStartXPx + charHalfWidthPx;
  const int asciiStartXPx =
      hexGlyphStartXPx + ((BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidthPx);

  layout.charWidth = pxToLogical(charWidthPx, layout.dpr);
  // Keep row spacing on the original logical line-height model; the vertex shaders
  // only snap X so rendering stays aligned with scroll, selection, and hit testing.
  layout.lineHeight = static_cast<float>(frame.lineHeight);
  layout.hexStartX = pxToLogical(hexStartXPx, layout.dpr);
  layout.hexGlyphStartX = pxToLogical(hexGlyphStartXPx, layout.dpr);
  layout.hexWidth = pxToLogical(BYTES_PER_LINE * 3 * charWidthPx, layout.dpr);
  layout.asciiStartX = pxToLogical(asciiStartXPx, layout.dpr);
  layout.asciiWidth =
      frame.shouldDrawAscii ? pxToLogical(BYTES_PER_LINE * charWidthPx, layout.dpr) : 0.0f;
  return layout;
}

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

  const int baseUboSize = m_rhi->ubufAligned(kMat4Bytes + kVec4Bytes);
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
  if (!m_frameTimer.isValid()) {
    m_frameTimer.start();
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
  delete m_outputRectPso;
  m_outputRectPso = nullptr;
  delete m_outputGlyphPso;
  m_outputGlyphPso = nullptr;

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
  delete m_selectionMaskRectBuf;
  m_selectionMaskRectBuf = nullptr;
  delete m_selectionEdgeRectBuf;
  m_selectionEdgeRectBuf = nullptr;
  delete m_playbackMaskRectBuf;
  m_playbackMaskRectBuf = nullptr;
  delete m_playbackEdgeRectBuf;
  m_playbackEdgeRectBuf = nullptr;
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

// Mark playback-only overlay geometry as stale.
void HexViewRhiRenderer::markPlaybackDirty() {
  m_playbackDirty = true;
}

// Drop CPU line cache and force a full geometry/id rebuild.
void HexViewRhiRenderer::invalidateCache() {
  m_cachedLines.clear();
  m_cacheStartLine = 0;
  m_cacheEndLine = -1;
  m_baseDirty = true;
  m_selectionDirty = true;
  m_playbackDirty = true;
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
    m_playbackDirty = true;
  }

  ensureCacheWindow(startLine, endLine, totalLines, frame);
  const LayoutMetrics layout = computeLayoutMetrics(frame);

  if (m_baseDirty) {
    buildBaseInstances(frame, layout);
    m_baseDirty = false;
    m_baseBufferDirty = true;
  }

  if (m_selectionDirty) {
    buildSelectionEffectInstances(startLine, endLine, frame, layout);
    m_selectionDirty = false;
    m_selectionBufferDirty = true;
  }
  if (m_playbackDirty) {
    buildPlaybackEffectInstances(startLine, endLine, frame, layout);
    m_playbackDirty = false;
    m_playbackBufferDirty = true;
  }

  QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
  if (!m_staticBuffersUploaded) {
    u->uploadStaticBuffer(m_vbuf, kVertices);
    u->uploadStaticBuffer(m_ibuf, kIndices);
    m_staticBuffersUploaded = true;
  }
  ensureGlyphTexture(u, frame);
  const float minCell = std::min(layout.charWidth, layout.lineHeight);
  const bool outlineAllowed = minCell >= OUTLINE_MIN_CELL_PX;
  const float nowSeconds = m_frameTimer.isValid() ? (m_frameTimer.elapsed() / 1000.0f) : 0.0f;
  float dt = (m_lastFrameSeconds > 0.0f) ? (nowSeconds - m_lastFrameSeconds) : 0.0f;
  m_lastFrameSeconds = nowSeconds;
  if (dt > 0.25f) {
    dt = 0.25f;
  }
  // Outline visibility is animated in CPU-side state so shaders can stay stateless.
  const float targetOutline = (outlineAllowed && frame.seekModifierActive) ? OUTLINE_ALPHA : 0.0f;
  if (targetOutline != m_outlineTarget) {
    m_outlineTarget = targetOutline;
    dt = 0.0f;  // avoid a large first step after long idle periods
  }
  if (m_outlineAlpha < targetOutline) {
    m_outlineAlpha = std::min(targetOutline, m_outlineAlpha + OUTLINE_FADE_SPEED * dt);
  } else if (m_outlineAlpha > targetOutline) {
    m_outlineAlpha = std::max(targetOutline, m_outlineAlpha - OUTLINE_FADE_SPEED * dt);
  }
  const bool outlineEnabled = outlineAllowed && (m_outlineAlpha > 0.001f || targetOutline > 0.0f);
  if (outlineEnabled != m_outlineEnabled) {
    m_outlineEnabled = outlineEnabled;
    m_itemIdDirty = true;
  }
  if (std::abs(m_outlineAlpha - targetOutline) > 0.001f) {
    m_view->requestPlaybackFrame();
  }
  const bool hasSelections = !frame.selections.empty() || !frame.fadeSelections.empty();
  const bool hasPlayback = !frame.playbackSelections.empty() || !frame.fadePlaybackSelections.empty();
  const bool needsComposite = hasSelections || hasPlayback || outlineEnabled ||
                              frame.overlayOpacity > 0.001;
  const bool edgeEnabled = needsComposite &&
      ((frame.shadowBlur > 0.0 && frame.shadowStrength > 0.0) ||
       (frame.playbackGlowRadius > 0.0f && frame.playbackGlowStrength > 0.0f));

  ensureItemIdTexture(u, startLine, endLine, totalLines, frame);
  ensureRenderTargets(target.pixelSize);
  const int sampleCount = std::max(1, target.sampleCount);
  ensurePipelines(target.renderPassDesc, sampleCount);
  if (m_compositeSrbDirty && m_compositeSrb) {
    updateCompositeSrb();
    m_compositeSrbDirty = false;
  }
  updateUniforms(u, static_cast<float>(scrollY), target.pixelSize, frame, layout);
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
  const QRhiViewport viewport(0, 0, target.pixelSize.width(), target.pixelSize.height());

  auto drawInstanced = [&](QRhiGraphicsPipeline* pso,
                           QRhiShaderResourceBindings* srb,
                           QRhiBuffer* instanceBuffer,
                           int count,
                           int firstInstance,
                           int instanceStride) {
    if (!pso || !srb || !instanceBuffer || count <= 0) {
      return;
    }
    quint32 instanceOffset = 0;
    int drawFirstInstance = firstInstance;
    if (!m_supportsBaseInstance && firstInstance > 0) {
      instanceOffset = static_cast<quint32>(firstInstance * instanceStride);
      drawFirstInstance = 0;
    }
    cb->setGraphicsPipeline(pso);
    cb->setShaderResources(srb);
    const QRhiCommandBuffer::VertexInput bindings[] = {
        {m_vbuf, 0},
        {instanceBuffer, instanceOffset},
    };
    cb->setVertexInput(0, 2, bindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, count, 0, 0, drawFirstInstance);
  };

  auto drawFullscreen = [&](QRhiGraphicsPipeline* pso, QRhiShaderResourceBindings* srb) {
    if (!pso || !srb) {
      return;
    }
    cb->setGraphicsPipeline(pso);
    cb->setShaderResources(srb);
    const QRhiCommandBuffer::VertexInput bindings[] = {{m_vbuf, 0}};
    cb->setVertexInput(0, 1, bindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, 1, 0, 0, 0);
  };

  // Submit uploads before the first pass so render-pass startup does not also
  // carry resource-update work on backends that are sensitive to it.
  if (u) {
    cb->resourceUpdate(u);
  }

  if (!needsComposite) {
    cb->beginPass(target.renderTarget, clearColor, {1.0f, 0}, nullptr);
    cb->setViewport(viewport);
    drawInstanced(m_outputRectPso,
                  m_rectSrb,
                  m_baseRectBuf,
                  baseRectCount,
                  baseRectFirst,
                  static_cast<int>(sizeof(RectInstance)));
    drawInstanced(m_outputGlyphPso,
                  m_glyphSrb,
                  m_baseGlyphBuf,
                  baseGlyphCount,
                  baseGlyphFirst,
                  static_cast<int>(sizeof(GlyphInstance)));
    cb->endPass();
    return;
  }

  // Pass 1: base content (background spans + glyphs) into an offscreen texture.
  cb->beginPass(m_contentRt, clearColor, {1.0f, 0}, nullptr);
  cb->setViewport(viewport);
  drawInstanced(m_rectPso,
                m_rectSrb,
                m_baseRectBuf,
                baseRectCount,
                baseRectFirst,
                static_cast<int>(sizeof(RectInstance)));
  drawInstanced(m_glyphPso,
                m_glyphSrb,
                m_baseGlyphBuf,
                baseGlyphCount,
                baseGlyphFirst,
                static_cast<int>(sizeof(GlyphInstance)));
  cb->endPass();

  // Pass 2: combined overlay/effect pass writes mask, edge falloff, and playback tint together.
  cb->beginPass(m_effectRt, QColor(0, 0, 0, 0), {1.0f, 0}, nullptr);
  cb->setViewport(viewport);
  drawInstanced(m_maskPso,
                m_rectSrb,
                m_selectionMaskRectBuf,
                static_cast<int>(m_selectionMaskRectInstances.size()),
                0,
                static_cast<int>(sizeof(RectInstance)));
  drawInstanced(m_maskPso,
                m_rectSrb,
                m_playbackMaskRectBuf,
                static_cast<int>(m_playbackMaskRectInstances.size()),
                0,
                static_cast<int>(sizeof(RectInstance)));
  if (edgeEnabled) {
    drawInstanced(m_edgePso,
                  m_edgeSrb,
                  m_selectionEdgeRectBuf,
                  static_cast<int>(m_selectionEdgeRectInstances.size()),
                  0,
                  static_cast<int>(sizeof(EdgeInstance)));
    drawInstanced(m_edgePso,
                  m_edgeSrb,
                  m_playbackEdgeRectBuf,
                  static_cast<int>(m_playbackEdgeRectInstances.size()),
                  0,
                  static_cast<int>(sizeof(EdgeInstance)));
  }
  cb->endPass();

  // Pass 3: fullscreen composite combines content + mask + playback color + edge + item-id textures.
  cb->beginPass(target.renderTarget, clearColor, {1.0f, 0}, nullptr);
  cb->setViewport(viewport);
  drawFullscreen(m_compositePso, m_compositeSrb);
  cb->endPass();
}

// Ensure offscreen content/effect render targets match current output pixel size.
void HexViewRhiRenderer::ensureRenderTargets(const QSize& pixelSize) {
  if (!m_rhi || pixelSize.isEmpty()) {
    return;
  }
  if (m_contentTex && m_contentTex->pixelSize() == pixelSize) {
    return;
  }

  releaseRenderTargets();

  auto makeTexture = [&](QRhiTexture** tex) {
    *tex = m_rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1, QRhiTexture::RenderTarget);
    (*tex)->create();
  };

  auto makeSingleTarget = [&](QRhiTexture** tex, QRhiTextureRenderTarget** rt,
                              QRhiRenderPassDescriptor** rp) {
    makeTexture(tex);
    *rt = m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(*tex));
    *rp = (*rt)->newCompatibleRenderPassDescriptor();
    (*rt)->setRenderPassDescriptor(*rp);
    (*rt)->create();
  };

  makeSingleTarget(&m_contentTex, &m_contentRt, &m_contentRp);
  makeTexture(&m_maskTex);
  makeTexture(&m_edgeTex);
  makeTexture(&m_playbackColorTex);

  QRhiTextureRenderTargetDescription effectDesc;
  effectDesc.setColorAttachments({QRhiColorAttachment(m_maskTex),
                                  QRhiColorAttachment(m_edgeTex),
                                  QRhiColorAttachment(m_playbackColorTex)});
  m_effectRt = m_rhi->newTextureRenderTarget(effectDesc);
  m_effectRp = m_effectRt->newCompatibleRenderPassDescriptor();
  m_effectRt->setRenderPassDescriptor(m_effectRp);
  m_effectRt->create();

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

  delete m_maskTex;
  m_maskTex = nullptr;

  delete m_effectRp;
  m_effectRp = nullptr;
  delete m_effectRt;
  m_effectRt = nullptr;
  delete m_playbackColorTex;
  m_playbackColorTex = nullptr;

  delete m_edgeTex;
  m_edgeTex = nullptr;
}

// Build or rebuild pipelines and SRBs when RP descriptors/samples/bindings change.
void HexViewRhiRenderer::ensurePipelines(QRhiRenderPassDescriptor* outputRp,
                                         int outputSampleCount) {
  if (!outputRp || !m_contentRp || !m_effectRp) {
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

  auto resetOwned = [](auto*& ptr) {
    delete ptr;
    ptr = nullptr;
  };
  resetOwned(m_rectPso);
  resetOwned(m_glyphPso);
  resetOwned(m_maskPso);
  resetOwned(m_edgePso);
  resetOwned(m_compositePso);
  resetOwned(m_outputRectPso);
  resetOwned(m_outputGlyphPso);
  resetOwned(m_rectSrb);
  resetOwned(m_glyphSrb);
  resetOwned(m_edgeSrb);
  resetOwned(m_compositeSrb);

  auto createSrb = [&](QRhiShaderResourceBindings*& srb,
                       std::initializer_list<QRhiShaderResourceBinding> bindings) {
    srb = m_rhi->newShaderResourceBindings();
    srb->setBindings(bindings);
    srb->create();
  };
  createSrb(m_rectSrb, {
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
  });
  createSrb(m_glyphSrb, {
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf),
      QRhiShaderResourceBinding::sampledTexture(1,
                                                QRhiShaderResourceBinding::FragmentStage,
                                                m_glyphTex,
                                                m_glyphSampler),
  });
  createSrb(m_edgeSrb, {
      QRhiShaderResourceBinding::uniformBuffer(
          0,
          QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
          m_edgeUbuf),
  });
  m_compositeSrb = m_rhi->newShaderResourceBindings();
  updateCompositeSrb();
  m_compositeSrbDirty = false;

  const QShader rectVert = loadShader(":/shaders/hexquad.vert.qsb");
  const QShader rectFrag = loadShader(":/shaders/hexquad.frag.qsb");
  const QShader glyphVert = loadShader(":/shaders/hexglyph.vert.qsb");
  const QShader glyphFrag = loadShader(":/shaders/hexglyph.frag.qsb");
  const QShader screenVert = loadShader(":/shaders/hexscreen.vert.qsb");
  const QShader edgeVert = loadShader(":/shaders/hexedge.vert.qsb");
  const QShader edgeEffectFrag = loadShader(":/shaders/hexedge_effect.frag.qsb");
  const QShader compositeFrag = loadShader(":/shaders/hexcomposite.frag.qsb");

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
    {1, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
    {1, 4, QRhiVertexInputAttribute::Float4, 12 * sizeof(float)}
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
  edgeBlend.opColor = QRhiGraphicsPipeline::Max;
  edgeBlend.srcAlpha = QRhiGraphicsPipeline::One;
  edgeBlend.dstAlpha = QRhiGraphicsPipeline::One;
  edgeBlend.opAlpha = QRhiGraphicsPipeline::Max;

  QRhiGraphicsPipeline::TargetBlend noWriteBlend;
  noWriteBlend.colorWrite = {};

  auto createPipeline = [&](QRhiGraphicsPipeline*& pso,
                            const QShader& vs,
                            const QShader& fs,
                            const QRhiVertexInputLayout& inputLayout,
                            QRhiShaderResourceBindings* srb,
                            std::initializer_list<QRhiGraphicsPipeline::TargetBlend> targetBlends,
                            int sampleCount,
                            QRhiRenderPassDescriptor* rp) {
    pso = m_rhi->newGraphicsPipeline();
    pso->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    pso->setVertexInputLayout(inputLayout);
    pso->setShaderResourceBindings(srb);
    pso->setCullMode(QRhiGraphicsPipeline::None);
    pso->setTargetBlends(targetBlends);
    pso->setSampleCount(sampleCount);
    pso->setRenderPassDescriptor(rp);
    pso->create();
  };

  createPipeline(m_rectPso,
                 rectVert,
                 rectFrag,
                 rectInputLayout,
                 m_rectSrb,
                 {blend},
                 1,
                 m_contentRp);
  createPipeline(m_glyphPso,
                 glyphVert,
                 glyphFrag,
                 glyphInputLayout,
                 m_glyphSrb,
                 {blend},
                 1,
                 m_contentRp);
  createPipeline(m_maskPso,
                 rectVert,
                 rectFrag,
                 rectInputLayout,
                 m_rectSrb,
                 {maskBlend, noWriteBlend, noWriteBlend},
                 1,
                 m_effectRp);
  createPipeline(m_edgePso,
                 edgeVert,
                 edgeEffectFrag,
                 edgeInputLayout,
                 m_edgeSrb,
                 {noWriteBlend, edgeBlend, blend},
                 1,
                 m_effectRp);
  createPipeline(m_compositePso,
                 screenVert,
                 compositeFrag,
                 screenInputLayout,
                 m_compositeSrb,
                 {},
                 m_sampleCount,
                 m_outputRp);
  createPipeline(m_outputRectPso,
                 rectVert,
                 rectFrag,
                 rectInputLayout,
                 m_rectSrb,
                 {blend},
                 m_sampleCount,
                 m_outputRp);
  createPipeline(m_outputGlyphPso,
                 glyphVert,
                 glyphFrag,
                 glyphInputLayout,
                 m_glyphSrb,
                 {blend},
                 m_sampleCount,
                 m_outputRp);
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
                                              m_playbackColorTex, m_glyphSampler),
    QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage,
                                              m_itemIdTex, m_maskSampler)
  });
  m_compositeSrb->create();
}

// Upload per-frame transforms and effect/shader parameters to uniform buffers.
void HexViewRhiRenderer::updateUniforms(QRhiResourceUpdateBatch* u, float scrollY,
                                        const QSize& pixelSize,
                                        const HexViewFrame::Data& frame,
                                        const LayoutMetrics& layout) {
  const QSize viewSize = frame.viewportSize;
  const float uvFlipY = m_rhi->isYUpInFramebuffer() ? 0.0f : 1.0f;

  QMatrix4x4 proj;
  proj.ortho(0.f, float(viewSize.width()), float(viewSize.height()), 0.f, -1.f, 1.f);

  QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix() * proj;
  mvp.translate(0.f, -scrollY, 0.f);

  u->updateDynamicBuffer(m_ubuf, 0, kMat4Bytes, mvp.constData());
  const QVector4D snapInfo(layout.dpr > 0.0f ? layout.dpr : 1.0f, 0.0f, 0.0f, 0.0f);
  u->updateDynamicBuffer(m_ubuf, kMat4Bytes, kVec4Bytes, &snapInfo);

  QMatrix4x4 screenMvp = m_rhi->clipSpaceCorrMatrix();
  const float dpr = layout.dpr;

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

  const float charWidth = layout.charWidth;
  const float lineHeight = layout.lineHeight;
  const float hexStartX = layout.hexStartX;
  const float hexWidth = layout.hexWidth;
  const float asciiStartX = layout.asciiStartX;
  const float asciiWidth = layout.asciiWidth;

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
  const QVector4D viewInfo(viewW, viewH, uvFlipY, dpr);
  const QVector4D effectInfo(frame.playbackGlowStrength, 0.0f, 0.0f, 0.0f);
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
                         &viewInfo);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 3, kVec4Bytes,
                         &effectInfo);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 4, kVec4Bytes,
                         &outlineColor);
  u->updateDynamicBuffer(m_compositeUbuf, kMat4Bytes + kVec4Bytes * 5, kVec4Bytes,
                         &itemIdWindow);
}

// Ensure dynamic instance buffer exists and can hold current data; grow geometrically.
bool HexViewRhiRenderer::ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes) {
  const int requiredBytes = std::max(1, bytes);
  if (buffer && buffer->size() >= static_cast<quint32>(requiredBytes)) {
    return false;
  }

  int allocBytes = requiredBytes;
  if (buffer) {
    allocBytes = std::max(requiredBytes, static_cast<int>(buffer->size()) * 2);
  } else {
    allocBytes = std::max(requiredBytes, 1024);
  }

  delete buffer;
  buffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, allocBytes);
  buffer->create();
  return true;
}

// Upload dirty base/selection/playback instance arrays to their corresponding dynamic buffers.
void HexViewRhiRenderer::updateInstanceBuffers(QRhiResourceUpdateBatch* u) {
  auto uploadIfDirty = [&](QRhiBuffer*& buffer, const void* data, int bytes, bool dirty) {
    const bool resized = ensureInstanceBuffer(buffer, bytes);
    if ((dirty || resized) && bytes > 0 && data) {
      u->updateDynamicBuffer(buffer, 0, bytes, data);
    }
  };

  const int baseRectBytes = static_cast<int>(m_baseRectInstances.size() * sizeof(RectInstance));
  const int baseGlyphBytes = static_cast<int>(m_baseGlyphInstances.size() * sizeof(GlyphInstance));
  const int selectionMaskRectBytes =
      static_cast<int>(m_selectionMaskRectInstances.size() * sizeof(RectInstance));
  const int selectionEdgeRectBytes =
      static_cast<int>(m_selectionEdgeRectInstances.size() * sizeof(EdgeInstance));
  const int playbackMaskRectBytes =
      static_cast<int>(m_playbackMaskRectInstances.size() * sizeof(RectInstance));
  const int playbackEdgeRectBytes =
      static_cast<int>(m_playbackEdgeRectInstances.size() * sizeof(EdgeInstance));

  uploadIfDirty(m_baseRectBuf, m_baseRectInstances.data(), baseRectBytes, m_baseBufferDirty);
  uploadIfDirty(m_baseGlyphBuf, m_baseGlyphInstances.data(), baseGlyphBytes, m_baseBufferDirty);
  uploadIfDirty(m_selectionMaskRectBuf,
                m_selectionMaskRectInstances.data(),
                selectionMaskRectBytes,
                m_selectionBufferDirty);
  uploadIfDirty(m_selectionEdgeRectBuf,
                m_selectionEdgeRectInstances.data(),
                selectionEdgeRectBytes,
                m_selectionBufferDirty);
  uploadIfDirty(m_playbackMaskRectBuf,
                m_playbackMaskRectInstances.data(),
                playbackMaskRectBytes,
                m_playbackBufferDirty);
  uploadIfDirty(m_playbackEdgeRectBuf,
                m_playbackEdgeRectInstances.data(),
                playbackEdgeRectBytes,
                m_playbackBufferDirty);

  m_baseBufferDirty = false;
  m_selectionBufferDirty = false;
  m_playbackBufferDirty = false;
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
                                       float h, float pad, const QVector4D& flags,
                                       const QVector4D& tint) {
  const float geomX = x - pad;
  const float geomY = y - pad;
  const float geomW = w + pad * 2.0f;
  const float geomH = h + pad * 2.0f;
  rects.push_back({geomX, geomY, geomW, geomH,
                   x, y, w, h,
                   flags.x(), flags.y(), flags.z(), flags.w(),
                   tint.x(), tint.y(), tint.z(), tint.w()});
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
  m_playbackDirty = true;
}

// Rebuild cached line bytes and style ids for the current cache window.
void HexViewRhiRenderer::rebuildCacheWindow(const HexViewFrame::Data& frame) {
  m_cachedLines.clear();
  if (m_cacheEndLine < m_cacheStartLine || !frame.vgmfile) {
    return;
  }

  const uint32_t fileLength = frame.vgmfile->length();
  const auto* baseData = reinterpret_cast<const uint8_t*>(frame.vgmfile->data());
  const auto styleIds = frame.styleIds;
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
          entry.styles[i] = (idx >= 0 && idx < static_cast<int>(styleIds.size()))
                                ? styleIds[static_cast<size_t>(idx)]
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
void HexViewRhiRenderer::buildBaseInstances(const HexViewFrame::Data& frame,
                                            const LayoutMetrics& layout) {
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

  const float charWidth = layout.charWidth;
  const float lineHeight = layout.lineHeight;
  const float hexStartX = layout.hexStartX;
  const float hexGlyphStartX = layout.hexGlyphStartX;
  const float asciiStartX = layout.asciiStartX;

  const auto styles = frame.styles;
  const HexViewFrame::Style fallbackStyle{clearColor, frame.windowTextColor};
  auto styleFor = [&](uint16_t styleId) -> const HexViewFrame::Style& {
    if (styleId < styles.size()) {
      return styles[styleId];
    }
    return fallbackStyle;
  };

  static const char kHexDigits[] = "0123456789ABCDEF";
  std::array<QRectF, 16> hexUvs{};
  for (int i = 0; i < 16; ++i) {
    hexUvs[i] = glyphUv(QLatin1Char(kHexDigits[i]), frame);
  }
  const QRectF spaceUv = glyphUv(QLatin1Char(' '), frame);
  std::array<QRectF, 256> asciiUvs{};
  if (frame.shouldDrawAscii) {
    const QRectF fallbackUv = glyphUv(QLatin1Char('.'), frame);
    asciiUvs.fill(fallbackUv);
    for (int c = 0x20; c <= 0x7E; ++c) {
      asciiUvs[static_cast<size_t>(c)] = glyphUv(QLatin1Char(static_cast<char>(c)), frame);
    }
  }

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
          const float hexX = hexStartX + spanStart * 3.0f * charWidth;
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
      const float hexX = hexGlyphStartX + i * 3.0f * charWidth;
      appendGlyph(m_baseGlyphInstances, hexX, y, charWidth, lineHeight, hexUvs[value >> 4], textColor);
      appendGlyph(m_baseGlyphInstances, hexX + charWidth, y, charWidth, lineHeight,
                  hexUvs[value & 0x0F], textColor);
      appendGlyph(m_baseGlyphInstances, hexX + 2.0f * charWidth, y, charWidth, lineHeight,
                  spaceUv, textColor);

      if (frame.shouldDrawAscii) {
        const uint8_t asciiChar = isPrintable(value) ? value : static_cast<uint8_t>('.');
        const float asciiX = asciiStartX + i * charWidth;
        appendGlyph(m_baseGlyphInstances,
                    asciiX,
                    y,
                    charWidth,
                    lineHeight,
                    asciiUvs[static_cast<size_t>(asciiChar)],
                    textColor);
      }
    }

    range.rectCount = static_cast<uint32_t>(m_baseRectInstances.size()) - range.rectStart;
    range.glyphCount = static_cast<uint32_t>(m_baseGlyphInstances.size()) - range.glyphStart;
    m_lineRanges.push_back(range);
  }
}

// Build static selection mask and shadow geometry for the current viewport.
void HexViewRhiRenderer::buildSelectionEffectInstances(int startLine, int endLine,
                                                       const HexViewFrame::Data& frame,
                                                       const LayoutMetrics& layout) {
  m_selectionMaskRectInstances.clear();
  m_selectionEdgeRectInstances.clear();

  const auto selections = frame.selections.empty() ? frame.fadeSelections : frame.selections;
  if (selections.empty() || startLine > endLine) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount <= 0) {
    return;
  }

  m_selectionMaskRectInstances.reserve(static_cast<size_t>(visibleCount) *
                                       (frame.shouldDrawAscii ? 8u : 4u));
  m_selectionEdgeRectInstances.reserve(static_cast<size_t>(visibleCount) *
                                       (frame.shouldDrawAscii ? 8u : 4u));

  SelectionBuildContext ctx;
  ctx.startLine = startLine;
  ctx.endLine = endLine;
  ctx.visibleCount = visibleCount;
  ctx.fileBaseOffset = frame.vgmfile ? frame.vgmfile->offset() : 0;
  ctx.fileLength = frame.vgmfile ? frame.vgmfile->length() : 0;
  ctx.charWidth = layout.charWidth;
  ctx.lineHeight = layout.lineHeight;
  ctx.hexStartX = layout.hexStartX;
  ctx.asciiStartX = layout.asciiStartX;
  ctx.shouldDrawAscii = frame.shouldDrawAscii;

  std::vector<uint8_t> lineBytes(static_cast<size_t>(visibleCount), 0);
  std::vector<uint16_t> lineMasks(static_cast<size_t>(visibleCount), 0);
  for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
    const CachedLine* entry = cachedLineFor(line);
    if (entry && entry->bytes > 0) {
      lineBytes[static_cast<size_t>(line - ctx.startLine)] =
          static_cast<uint8_t>(std::min(entry->bytes, kBytesPerLine));
    }
  }

  auto spanMaskBits = [](int startCol, int endCol) -> uint16_t {
    const int start = std::clamp(startCol, 0, kBytesPerLine);
    const int end = std::clamp(endCol, 0, kBytesPerLine);
    if (end <= start) {
      return 0;
    }
    const uint32_t lowMask = (start > 0) ? ((uint32_t(1) << start) - 1u) : 0u;
    const uint32_t highMask = (end >= kBytesPerLine)
                                  ? 0xFFFFu
                                  : ((uint32_t(1) << end) - 1u);
    return static_cast<uint16_t>(highMask & ~lowMask);
  };
  auto appendMaskSpan = [&](std::vector<RectInstance>& rects,
                            int line,
                            int startCol,
                            int endCol,
                            const QVector4D& color) {
    const int spanLen = endCol - startCol;
    if (spanLen <= 0) {
      return;
    }
    const float y = line * ctx.lineHeight;
    const float hexX = ctx.hexStartX + startCol * 3.0f * ctx.charWidth;
    appendRect(rects, hexX, y, spanLen * 3.0f * ctx.charWidth, ctx.lineHeight, color);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + startCol * ctx.charWidth;
      appendRect(rects, asciiX, y, spanLen * ctx.charWidth, ctx.lineHeight, color);
    }
  };
  auto appendEdgeSpan = [&](std::vector<EdgeInstance>& rects,
                            int line,
                            int startCol,
                            int endCol,
                            float pad,
                            const QVector4D& flags,
                            const QVector4D& tint) {
    const int spanLen = endCol - startCol;
    if (spanLen <= 0) {
      return;
    }
    const float y = line * ctx.lineHeight;
    const float hexX = ctx.hexStartX + startCol * 3.0f * ctx.charWidth;
    appendEdgeRect(rects, hexX, y, spanLen * 3.0f * ctx.charWidth, ctx.lineHeight, pad, flags,
                   tint);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + startCol * ctx.charWidth;
      appendEdgeRect(rects, asciiX, y, spanLen * ctx.charWidth, ctx.lineHeight, pad, flags, tint);
    }
  };

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
      const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
      const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
      if (lineByteCount <= 0) {
        continue;
      }
      const int lineOffset = line * kBytesPerLine;
      const int startCol = (line == selStartLine) ? (selectionStart - lineOffset) : 0;
      const int endCol = (line == selEndLine) ? (selectionEnd - lineOffset) : lineByteCount;
      const int clampedStart = std::clamp(startCol, 0, lineByteCount);
      const int clampedEnd = std::clamp(endCol, 0, lineByteCount);
      if (clampedEnd > clampedStart) {
        lineMasks[lineIndex] |= spanMaskBits(clampedStart, clampedEnd);
      }
    }
  }

  const QVector4D selectionMaskColor(1.0f, 0.0f, 0.0f, 0.0f);
  const QVector4D selectionEdgeFlags(1.0f, 0.0f, 0.0f, 1.0f);
  const QVector4D noTint(0.0f, 0.0f, 0.0f, 0.0f);
  const float shadowPad =
      (frame.shadowBlur > 0.0 && frame.shadowStrength > 0.0)
          ? std::max(0.0f, static_cast<float>(frame.shadowBlur))
          : 0.0f;

  for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
    const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
    const uint16_t maskBits = lineMasks[lineIndex];
    const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
    int col = 0;
    while (col < lineByteCount) {
      while (col < lineByteCount && (maskBits & static_cast<uint16_t>(1u << col)) == 0u) {
        ++col;
      }
      if (col >= lineByteCount) {
        break;
      }
      const int startCol = col;
      while (col < lineByteCount && (maskBits & static_cast<uint16_t>(1u << col)) != 0u) {
        ++col;
      }
      const int endCol = col;
      appendMaskSpan(m_selectionMaskRectInstances, line, startCol, endCol, selectionMaskColor);
      if (shadowPad > 0.0f) {
        appendEdgeSpan(m_selectionEdgeRectInstances, line, startCol, endCol, shadowPad,
                       selectionEdgeFlags, noTint);
      }
    }
  }
}

// Build dynamic playback mask, fade-alpha, and glow/tint geometry for the current viewport.
void HexViewRhiRenderer::buildPlaybackEffectInstances(int startLine, int endLine,
                                                      const HexViewFrame::Data& frame,
                                                      const LayoutMetrics& layout) {
  m_playbackMaskRectInstances.clear();
  m_playbackEdgeRectInstances.clear();

  const bool hasPlayback = !frame.playbackSelections.empty() || !frame.fadePlaybackSelections.empty();
  if (!hasPlayback || startLine > endLine) {
    return;
  }

  const int visibleCount = endLine - startLine + 1;
  if (visibleCount <= 0) {
    return;
  }

  m_playbackMaskRectInstances.reserve(static_cast<size_t>(visibleCount) *
                                      (frame.shouldDrawAscii ? 16u : 8u));
  m_playbackEdgeRectInstances.reserve(static_cast<size_t>(visibleCount) *
                                      (frame.shouldDrawAscii ? 16u : 8u));

  SelectionBuildContext ctx;
  ctx.startLine = startLine;
  ctx.endLine = endLine;
  ctx.visibleCount = visibleCount;
  ctx.fileBaseOffset = frame.vgmfile ? frame.vgmfile->offset() : 0;
  ctx.fileLength = frame.vgmfile ? frame.vgmfile->length() : 0;
  ctx.charWidth = layout.charWidth;
  ctx.lineHeight = layout.lineHeight;
  ctx.hexStartX = layout.hexStartX;
  ctx.asciiStartX = layout.asciiStartX;
  ctx.shouldDrawAscii = frame.shouldDrawAscii;

  std::vector<uint8_t> lineBytes(static_cast<size_t>(visibleCount), 0);
  std::vector<uint16_t> activeMasks(static_cast<size_t>(visibleCount), 0);
  std::vector<uint16_t> fadeMasks(static_cast<size_t>(visibleCount), 0);
  std::vector<std::array<QVector4D, kBytesPerLine>> activeColors(static_cast<size_t>(visibleCount));
  std::vector<std::array<QVector4D, kBytesPerLine>> fadeColors(static_cast<size_t>(visibleCount));
  std::vector<std::array<float, kBytesPerLine>> fadeAlphas(static_cast<size_t>(visibleCount));

  for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
    const CachedLine* entry = cachedLineFor(line);
    if (entry && entry->bytes > 0) {
      lineBytes[static_cast<size_t>(line - ctx.startLine)] =
          static_cast<uint8_t>(std::min(entry->bytes, kBytesPerLine));
    }
  }

  auto appendMaskSpan = [&](std::vector<RectInstance>& rects,
                            int line,
                            int startCol,
                            int endCol,
                            const QVector4D& color) {
    const int spanLen = endCol - startCol;
    if (spanLen <= 0) {
      return;
    }
    const float y = line * ctx.lineHeight;
    const float hexX = ctx.hexStartX + startCol * 3.0f * ctx.charWidth;
    appendRect(rects, hexX, y, spanLen * 3.0f * ctx.charWidth, ctx.lineHeight, color);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + startCol * ctx.charWidth;
      appendRect(rects, asciiX, y, spanLen * ctx.charWidth, ctx.lineHeight, color);
    }
  };
  auto appendEdgeSpan = [&](std::vector<EdgeInstance>& rects,
                            int line,
                            int startCol,
                            int endCol,
                            float pad,
                            const QVector4D& flags,
                            const QVector4D& tint) {
    const int spanLen = endCol - startCol;
    if (spanLen <= 0) {
      return;
    }
    const float y = line * ctx.lineHeight;
    const float hexX = ctx.hexStartX + startCol * 3.0f * ctx.charWidth;
    appendEdgeRect(rects, hexX, y, spanLen * 3.0f * ctx.charWidth, ctx.lineHeight, pad, flags,
                   tint);
    if (ctx.shouldDrawAscii) {
      const float asciiX = ctx.asciiStartX + startCol * ctx.charWidth;
      appendEdgeRect(rects, asciiX, y, spanLen * ctx.charWidth, ctx.lineHeight, pad, flags, tint);
    }
  };
  auto sameTint = [](const QVector4D& a, const QVector4D& b) {
    return std::abs(a.x() - b.x()) < 0.0001f && std::abs(a.y() - b.y()) < 0.0001f &&
           std::abs(a.z() - b.z()) < 0.0001f && std::abs(a.w() - b.w()) < 0.0001f;
  };
  auto populatePlaybackField =
      [&](const HexViewFrame::PlaybackSelection& selection,
          std::vector<uint16_t>& masks,
          std::vector<std::array<QVector4D, kBytesPerLine>>& colors,
          std::vector<std::array<float, kBytesPerLine>>* alphas,
          float alphaValue) {
        if (selection.length == 0) {
          return;
        }

        int selectionStart = static_cast<int>(selection.offset - ctx.fileBaseOffset);
        int selectionEnd = selectionStart + static_cast<int>(selection.length);
        if (selectionEnd <= 0 || selectionStart >= static_cast<int>(ctx.fileLength)) {
          return;
        }
        selectionStart = std::max(selectionStart, 0);
        selectionEnd = std::min(selectionEnd, static_cast<int>(ctx.fileLength));
        if (selectionEnd <= selectionStart) {
          return;
        }

        const QColor glowColor =
            selection.glowColor.isValid() ? selection.glowColor : QColor(Qt::white);
        const QVector4D tint(glowColor.redF(), glowColor.greenF(), glowColor.blueF(), 1.0f);
        const int selStartLine = selectionStart / kBytesPerLine;
        const int selEndLine = (selectionEnd - 1) / kBytesPerLine;
        const int lineStart = std::max(ctx.startLine, selStartLine);
        const int lineEnd = std::min(ctx.endLine, selEndLine);

        for (int line = lineStart; line <= lineEnd; ++line) {
          const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
          const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
          if (lineByteCount <= 0) {
            continue;
          }

          const int lineOffset = line * kBytesPerLine;
          const int startCol = (line == selStartLine) ? (selectionStart - lineOffset) : 0;
          const int endCol = (line == selEndLine) ? (selectionEnd - lineOffset) : lineByteCount;
          const int clampedStart = std::clamp(startCol, 0, lineByteCount);
          const int clampedEnd = std::clamp(endCol, 0, lineByteCount);
          if (clampedEnd <= clampedStart) {
            continue;
          }

          for (int col = clampedStart; col < clampedEnd; ++col) {
            masks[lineIndex] |= static_cast<uint16_t>(1u << col);
            colors[lineIndex][static_cast<size_t>(col)] = tint;
            if (alphas) {
              (*alphas)[lineIndex][static_cast<size_t>(col)] =
                  std::max((*alphas)[lineIndex][static_cast<size_t>(col)], alphaValue);
            }
          }
        }
      };
  auto emitMaskSpans = [&](const std::vector<uint16_t>& masks, const QVector4D& color) {
    for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
      const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
      const uint16_t maskBits = masks[lineIndex];
      const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
      int col = 0;
      while (col < lineByteCount) {
        while (col < lineByteCount && (maskBits & static_cast<uint16_t>(1u << col)) == 0u) {
          ++col;
        }
        if (col >= lineByteCount) {
          break;
        }
        const int startCol = col;
        while (col < lineByteCount && (maskBits & static_cast<uint16_t>(1u << col)) != 0u) {
          ++col;
        }
        appendMaskSpan(m_playbackMaskRectInstances, line, startCol, col, color);
      }
    }
  };
  auto emitFadeAlphaSpans = [&]() {
    for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
      const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
      const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
      int startCol = -1;
      float currentAlpha = 0.0f;
      for (int col = 0; col <= lineByteCount; ++col) {
        const float alpha =
            (col < lineByteCount) ? fadeAlphas[lineIndex][static_cast<size_t>(col)] : 0.0f;
        if (startCol < 0) {
          if (alpha > 0.0f) {
            startCol = col;
            currentAlpha = alpha;
          }
          continue;
        }
        if (col == lineByteCount || alpha <= 0.0f || std::abs(alpha - currentAlpha) > 0.0001f) {
          appendMaskSpan(m_playbackMaskRectInstances,
                         line,
                         startCol,
                         col,
                         QVector4D(0.0f, 0.0f, 1.0f, currentAlpha));
          startCol = (alpha > 0.0f) ? col : -1;
          currentAlpha = alpha;
        }
      }
    }
  };
  auto emitTintedEdgeSpans =
      [&](const std::vector<uint16_t>& masks,
          const std::vector<std::array<QVector4D, kBytesPerLine>>& colors,
          const QVector4D& flags,
          float pad) {
        if (pad <= 0.0f) {
          return;
        }
        for (int line = ctx.startLine; line <= ctx.endLine; ++line) {
          const size_t lineIndex = static_cast<size_t>(line - ctx.startLine);
          const uint16_t maskBits = masks[lineIndex];
          const int lineByteCount = static_cast<int>(lineBytes[lineIndex]);
          int col = 0;
          while (col < lineByteCount) {
            while (col < lineByteCount && (maskBits & static_cast<uint16_t>(1u << col)) == 0u) {
              ++col;
            }
            if (col >= lineByteCount) {
              break;
            }
            const int startCol = col;
            const QVector4D tint = colors[lineIndex][static_cast<size_t>(col)];
            ++col;
            while (col < lineByteCount &&
                   (maskBits & static_cast<uint16_t>(1u << col)) != 0u &&
                   sameTint(colors[lineIndex][static_cast<size_t>(col)], tint)) {
              ++col;
            }
            appendEdgeSpan(m_playbackEdgeRectInstances, line, startCol, col, pad, flags, tint);
          }
        }
      };

  for (const auto& selection : frame.playbackSelections) {
    populatePlaybackField(selection, activeMasks, activeColors, nullptr, 0.0f);
  }
  for (const auto& selection : frame.fadePlaybackSelections) {
    if (selection.alpha <= 0.0f) {
      continue;
    }
    populatePlaybackField(selection.range, fadeMasks, fadeColors, &fadeAlphas, selection.alpha);
  }

  emitMaskSpans(activeMasks, QVector4D(0.0f, 1.0f, 0.0f, 0.0f));
  emitMaskSpans(fadeMasks, QVector4D(0.0f, 0.0f, 1.0f, 0.0f));
  emitFadeAlphaSpans();

  const float glowBase = std::max(0.0f, static_cast<float>(frame.playbackGlowRadius)) *
                         std::max(1.0f, std::min(layout.charWidth, layout.lineHeight));
  const float glowPad =
      (frame.playbackGlowRadius > 0.0f && frame.playbackGlowStrength > 0.0f)
          ? (glowBase * 1.6f)
          : 0.0f;
  emitTintedEdgeSpans(activeMasks, activeColors, QVector4D(0.0f, 1.0f, 0.0f, 0.0f), glowPad);
  emitTintedEdgeSpans(fadeMasks, fadeColors, QVector4D(0.0f, 0.0f, 1.0f, 0.0f), glowPad);
}
