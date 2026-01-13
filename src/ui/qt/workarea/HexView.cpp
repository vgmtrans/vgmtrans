/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexView.h"
#include "Helpers.h"
#include "VGMFile.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <QApplication>
#include <QFile>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace {
constexpr int BYTES_PER_LINE = 16;
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 18;
constexpr int VIEWPORT_PADDING = 10;
constexpr int DIM_DURATION_MS = 200;
constexpr int OVERLAY_ALPHA = 80;
constexpr float OVERLAY_ALPHA_F = OVERLAY_ALPHA / 255.0f;
const QColor SHADOW_COLOR = Qt::black;
constexpr float SHADOW_OFFSET_X = 1.0f;
constexpr float SHADOW_OFFSET_Y = 4.0f;
constexpr float SHADOW_BLUR_RADIUS = SELECTION_PADDING * 2.0f;
constexpr float SHADOW_CORNER_RADIUS = 0.0f;
constexpr uint16_t STYLE_UNASSIGNED = std::numeric_limits<uint16_t>::max();

struct RectInstance {
  float x;
  float y;
  float w;
  float h;
  float r;
  float g;
  float b;
  float a;
};

struct GlyphInstance {
  float x;
  float y;
  float w;
  float h;
  float u0;
  float v0;
  float u1;
  float v1;
  float r;
  float g;
  float b;
  float a;
};

struct ShadowInstance {
  float gx;
  float gy;
  float gw;
  float gh;
  float rx;
  float ry;
  float rw;
  float rh;
  float r;
  float g;
  float b;
  float a;
};

struct CachedLine {
  int line = 0;
  int bytes = 0;
  std::array<uint8_t, BYTES_PER_LINE> data{};
  std::array<uint16_t, BYTES_PER_LINE> styles{};
};

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

struct HexView::GlyphAtlas {
  QImage image;
  std::array<QRectF, 128> uvTable{};
  qreal dpr = 0.0;
  int glyphWidth = 0;
  int glyphHeight = 0;
  int cellWidth = 0;
  int cellHeight = 0;
  uint64_t version = 0;
  QFont font;
};

HexView::~HexView() = default;

class HexViewViewport final : public QRhiWidget {
public:
  explicit HexViewViewport(HexView* view)
      : QRhiWidget(view), m_view(view) {
    setFocusPolicy(Qt::NoFocus);
  }

  void markBaseDirty() {
    m_baseDirty = true;
  }

  void markSelectionDirty() {
    m_selectionDirty = true;
  }

  void markOverlayDirty() {
    m_overlayDirty = true;
  }

  void invalidateCache() {
    m_cachedLines.clear();
    m_cacheStartLine = 0;
    m_cacheEndLine = -1;
    m_baseDirty = true;
    m_selectionDirty = true;
  }

protected:
  void initialize(QRhiCommandBuffer* cb) override {
    m_rhi = rhi();
    if (!m_rhi) {
      return;
    }

    static const float kVertices[] = {
      0.0f, 0.0f,
      1.0f, 0.0f,
      1.0f, 1.0f,
      0.0f, 1.0f,
    };
    static const quint16 kIndices[] = {0, 1, 2, 0, 2, 3};

    m_vbuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kVertices));
    m_vbuf->create();
    m_ibuf = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, sizeof(kIndices));
    m_ibuf->create();

    m_ubuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                              sizeof(QMatrix4x4) + sizeof(QVector4D));
    m_ubuf->create();

    m_shadowUbuf = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                    sizeof(QMatrix4x4) + sizeof(QVector4D) + sizeof(QVector4D));
    m_shadowUbuf->create();

    m_glyphSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                       QRhiSampler::None, QRhiSampler::ClampToEdge,
                                       QRhiSampler::ClampToEdge);
    m_glyphSampler->create();

    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf, kVertices);
    u->uploadStaticBuffer(m_ibuf, kIndices);
    cb->resourceUpdate(u);

    m_sampleCount = 0;
  }

  void render(QRhiCommandBuffer* cb) override {
    if (!m_rhi || !m_view || !m_view->m_vgmfile) {
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

    const int scrollY = m_view->verticalScrollBar()->value();
    const int startLine = std::clamp(scrollY / m_view->m_lineHeight, 0, totalLines - 1);
    const int endLine = std::clamp((scrollY + viewportHeight) / m_view->m_lineHeight, 0, totalLines - 1);

    if (startLine != m_lastStartLine || endLine != m_lastEndLine) {
      m_lastStartLine = startLine;
      m_lastEndLine = endLine;
      m_selectionDirty = true;
    }

    m_view->ensureGlyphAtlas(devicePixelRatioF());
    ensureCacheWindow(startLine, endLine, totalLines);

    if (m_baseDirty) {
      buildBaseInstances();
      m_baseDirty = false;
      m_baseBufferDirty = true;
    }

    if (m_view->m_overlayOpacity > 0.0f && scrollY != m_lastOverlayScrollY) {
      m_overlayDirty = true;
    }

    if (m_overlayDirty) {
      buildOverlayInstances(scrollY, viewportHeight);
      m_overlayDirty = false;
      m_overlayBufferDirty = true;
      m_lastOverlayScrollY = scrollY;
    }

    if (m_selectionDirty) {
      buildSelectionInstances(startLine, endLine);
      m_selectionDirty = false;
      m_selectionBufferDirty = true;
    }

    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
    ensureGlyphTexture(u);
    ensurePipelines();
    updateUniforms(u, static_cast<float>(scrollY));
    updateInstanceBuffers(u);

    cb->beginPass(renderTarget(), m_view->palette().color(QPalette::Window), {1.0f, 0}, u);
    cb->setViewport(QRhiViewport(0, 0, renderTarget()->pixelSize().width(),
                                 renderTarget()->pixelSize().height()));

    drawRectBuffer(cb, m_baseRectBuf, static_cast<int>(m_baseRectInstances.size()));
    drawGlyphBuffer(cb, m_baseGlyphBuf, static_cast<int>(m_baseGlyphInstances.size()));
    drawRectBuffer(cb, m_overlayRectBuf, static_cast<int>(m_overlayRectInstances.size()));
    drawShadowBuffer(cb);
    drawRectBuffer(cb, m_selectionRectBuf, static_cast<int>(m_selectionRectInstances.size()));
    drawGlyphBuffer(cb, m_selectionGlyphBuf, static_cast<int>(m_selectionGlyphInstances.size()));

    cb->endPass();
  }

  void releaseResources() override {
    delete m_rectPso;
    m_rectPso = nullptr;
    delete m_glyphPso;
    m_glyphPso = nullptr;
    delete m_shadowPso;
    m_shadowPso = nullptr;

    delete m_rectSrb;
    m_rectSrb = nullptr;
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
    delete m_shadowUbuf;
    m_shadowUbuf = nullptr;
  }

private:
  void ensurePipelines() {
    const int sampleCount = renderTarget()->sampleCount();
    if (m_rectPso && m_sampleCount == sampleCount) {
      return;
    }
    m_sampleCount = sampleCount;

    delete m_rectPso;
    delete m_glyphPso;
    delete m_shadowPso;
    delete m_rectSrb;
    delete m_glyphSrb;
    delete m_shadowSrb;

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

    m_shadowSrb = m_rhi->newShaderResourceBindings();
    m_shadowSrb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0,
                                               QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
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
    m_rectPso->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_rectPso->create();

    m_glyphPso = m_rhi->newGraphicsPipeline();
    m_glyphPso->setShaderStages({{QRhiShaderStage::Vertex, glyphVert},
                                 {QRhiShaderStage::Fragment, glyphFrag}});
    m_glyphPso->setVertexInputLayout(glyphInputLayout);
    m_glyphPso->setShaderResourceBindings(m_glyphSrb);
    m_glyphPso->setCullMode(QRhiGraphicsPipeline::None);
    m_glyphPso->setTargetBlends({blend});
    m_glyphPso->setSampleCount(m_sampleCount);
    m_glyphPso->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_glyphPso->create();

    m_shadowPso = m_rhi->newGraphicsPipeline();
    m_shadowPso->setShaderStages({{QRhiShaderStage::Vertex, shadowVert},
                                  {QRhiShaderStage::Fragment, shadowFrag}});
    m_shadowPso->setVertexInputLayout(shadowInputLayout);
    m_shadowPso->setShaderResourceBindings(m_shadowSrb);
    m_shadowPso->setCullMode(QRhiGraphicsPipeline::None);
    m_shadowPso->setTargetBlends({blend});
    m_shadowPso->setSampleCount(m_sampleCount);
    m_shadowPso->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_shadowPso->create();
  }

  void ensureGlyphTexture(QRhiResourceUpdateBatch* u) {
    if (!m_view->m_glyphAtlas || m_view->m_glyphAtlas->image.isNull()) {
      return;
    }

    const auto& atlas = *m_view->m_glyphAtlas;
    if (m_glyphTex && m_glyphAtlasVersion == atlas.version) {
      return;
    }

    delete m_glyphTex;
    m_glyphTex = m_rhi->newTexture(QRhiTexture::RGBA8, atlas.image.size(), 1);
                                   // QRhiTexture::UsedWithSampledTexture);
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

  void updateUniforms(QRhiResourceUpdateBatch* u, float scrollY) {
    QSize viewportSize = m_view->viewport()->size();
    QMatrix4x4 mvp;
    mvp.ortho(0.0f, static_cast<float>(viewportSize.width()),
              static_cast<float>(viewportSize.height()), 0.0f, -1.0f, 1.0f);

    QVector4D params(scrollY, 0.0f, 0.0f, 0.0f);
    u->updateDynamicBuffer(m_ubuf, 0, sizeof(QMatrix4x4), &mvp);
    u->updateDynamicBuffer(m_ubuf, sizeof(QMatrix4x4), sizeof(QVector4D), &params);

    QVector4D shadowParams(m_view->m_shadowOffset.x(), m_view->m_shadowOffset.y(),
                           m_view->m_shadowBlur, SHADOW_CORNER_RADIUS);
    QVector4D scrollParams(scrollY, 0.0f, 0.0f, 0.0f);

    u->updateDynamicBuffer(m_shadowUbuf, 0, sizeof(QMatrix4x4), &mvp);
    u->updateDynamicBuffer(m_shadowUbuf, sizeof(QMatrix4x4), sizeof(QVector4D), &shadowParams);
    u->updateDynamicBuffer(m_shadowUbuf, sizeof(QMatrix4x4) + sizeof(QVector4D),
                           sizeof(QVector4D), &scrollParams);
  }

  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes) {
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

  void updateInstanceBuffers(QRhiResourceUpdateBatch* u) {
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

  void drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count) {
    if (!buffer || count <= 0) {
      return;
    }
    cb->setGraphicsPipeline(m_rectPso);
    cb->setShaderResources(m_rectSrb);
    const QRhiCommandBuffer::VertexInput vbufBindings[] = {
      {m_vbuf, 0},
      {buffer, 0}
    };
    cb->setVertexInput(0, 2, vbufBindings, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, count);
  }

  void drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count) {
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
    cb->drawIndexed(6, count);
  }

  void drawShadowBuffer(QRhiCommandBuffer* cb) {
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

  QRectF glyphUv(const QChar& ch) const {
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

  void appendRect(std::vector<RectInstance>& rects, float x, float y, float w, float h,
                  const QVector4D& color) {
    rects.push_back({x, y, w, h, color.x(), color.y(), color.z(), color.w()});
  }

  void appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w, float h,
                   const QRectF& uv, const QVector4D& color) {
    if (uv.isNull()) {
      return;
    }
    glyphs.push_back({x, y, w, h,
                      static_cast<float>(uv.left()), static_cast<float>(uv.top()),
                      static_cast<float>(uv.right()), static_cast<float>(uv.bottom()),
                      color.x(), color.y(), color.z(), color.w()});
  }

  void appendShadow(const QRectF& rect, float pad, const QVector4D& color) {
    const float gx = static_cast<float>(rect.x() - pad);
    const float gy = static_cast<float>(rect.y() - pad);
    const float gw = static_cast<float>(rect.width() + pad * 2.0f);
    const float gh = static_cast<float>(rect.height() + pad * 2.0f);
    m_shadowInstances.push_back({gx, gy, gw, gh,
                                 static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                                 static_cast<float>(rect.width()), static_cast<float>(rect.height()),
                                 color.x(), color.y(), color.z(), color.w()});
  }

  void ensureCacheWindow(int startLine, int endLine, int totalLines) {
    const int visibleCount = endLine - startLine + 1;
    const int margin = std::max(visibleCount * 2, 1);
    const int desiredStart = std::max(0, startLine - margin);
    const int desiredEnd = std::min(totalLines - 1, endLine + margin);

    if (desiredStart != m_cacheStartLine || desiredEnd != m_cacheEndLine || m_cachedLines.empty()) {
      m_cacheStartLine = desiredStart;
      m_cacheEndLine = desiredEnd;
      rebuildCacheWindow();
      m_baseDirty = true;
      m_selectionDirty = true;
    }
  }

  void rebuildCacheWindow() {
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
      const int lineOffset = line * BYTES_PER_LINE;
      if (lineOffset < static_cast<int>(fileLength)) {
        entry.bytes = std::min(BYTES_PER_LINE, static_cast<int>(fileLength) - lineOffset);
        if (entry.bytes > 0) {
          std::copy_n(baseData + lineOffset, entry.bytes, entry.data.data());
          for (int i = 0; i < entry.bytes; ++i) {
            const int idx = lineOffset + i;
            entry.styles[i] =
                (idx >= 0 && idx < static_cast<int>(m_view->m_styleIds.size())) ? m_view->m_styleIds[idx] : 0;
          }
        }
      }
      m_cachedLines.push_back(entry);
    }
  }

  const CachedLine* cachedLineFor(int line) const {
    if (line < m_cacheStartLine || line > m_cacheEndLine) {
      return nullptr;
    }
    const size_t index = static_cast<size_t>(line - m_cacheStartLine);
    if (index >= m_cachedLines.size()) {
      return nullptr;
    }
    return &m_cachedLines[index];
  }

  void buildBaseInstances() {
    m_baseRectInstances.clear();
    m_baseGlyphInstances.clear();

    if (m_cachedLines.empty()) {
      return;
    }

    m_baseRectInstances.reserve(m_cachedLines.size() * 4);
    m_baseGlyphInstances.reserve(m_cachedLines.size() * (m_view->m_shouldDrawAscii ? 80 : 64));

    const QColor clearColor = m_view->palette().color(QPalette::Window);
    const QVector4D addressColor = toVec4(m_view->palette().color(QPalette::WindowText));

    const float charWidth = static_cast<float>(m_view->m_charWidth);
    const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
    const float lineHeight = static_cast<float>(m_view->m_lineHeight);

    const float hexStartX = static_cast<float>(m_view->hexXOffset());
    const float asciiStartX = hexStartX + (BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;

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
      const float y = entry.line * lineHeight;

      if (m_view->m_shouldDrawOffset) {
        uint32_t address = m_view->m_vgmfile->dwOffset + (entry.line * BYTES_PER_LINE);
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
    }
  }

  void buildOverlayInstances(int scrollY, int viewportHeight) {
    m_overlayRectInstances.clear();
    if (m_view->m_overlayOpacity <= 0.0f) {
      return;
    }

    m_overlayRectInstances.reserve(2);
    const float charWidth = static_cast<float>(m_view->m_charWidth);
    const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
    const float hexStartX = static_cast<float>(m_view->hexXOffset());
    const float asciiStartX = hexStartX + (BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;
    const QVector4D overlayColor(0.0f, 0.0f, 0.0f, static_cast<float>(m_view->m_overlayOpacity));

    const float y = static_cast<float>(scrollY);
    appendRect(m_overlayRectInstances, hexStartX - charHalfWidth, y,
               BYTES_PER_LINE * 3.0f * charWidth, static_cast<float>(viewportHeight), overlayColor);
    if (m_view->m_shouldDrawAscii) {
      appendRect(m_overlayRectInstances, asciiStartX, y,
                 BYTES_PER_LINE * charWidth, static_cast<float>(viewportHeight), overlayColor);
    }
  }

  void buildSelectionInstances(int startLine, int endLine) {
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
      m_selectionGlyphInstances.reserve(static_cast<size_t>(visibleCount) * (m_view->m_shouldDrawAscii ? 80 : 64));
      m_shadowInstances.reserve(static_cast<size_t>(visibleCount) * 2);
    }

    const uint32_t fileLength = m_view->m_vgmfile->unLength;
    const float charWidth = static_cast<float>(m_view->m_charWidth);
    const float charHalfWidth = static_cast<float>(m_view->m_charHalfWidth);
    const float lineHeight = static_cast<float>(m_view->m_lineHeight);
    const float hexStartX = static_cast<float>(m_view->hexXOffset());
    const float asciiStartX = hexStartX + (BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS) * charWidth;

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

    const QVector4D shadowColor = toVec4(SHADOW_COLOR);
    const float shadowPadBase = SHADOW_BLUR_RADIUS * 2.0f + SELECTION_PADDING +
                                std::max(std::abs(SHADOW_OFFSET_X), std::abs(SHADOW_OFFSET_Y));

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

      const int selStartLine = std::max(startLine, selectionStart / BYTES_PER_LINE);
      const int selEndLine = std::min(endLine, (selectionEnd - 1) / BYTES_PER_LINE);

      for (int line = selStartLine; line <= selEndLine; ++line) {
        const CachedLine* entry = cachedLineFor(line);
        if (!entry || entry->bytes <= 0) {
          continue;
        }

        const int lineOffset = line * BYTES_PER_LINE;
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
            appendShadow(hexRect, shadowPadBase, shadowColor);

            if (m_view->m_shouldDrawAscii) {
              const float asciiX = asciiStartX + spanStart * charWidth;
              const QRectF asciiRect(asciiX, y, spanLen * charWidth, lineHeight);
              appendRect(m_selectionRectInstances, asciiRect.x(), asciiRect.y(), asciiRect.width(),
                         asciiRect.height(), bgColor);
              appendShadow(asciiRect, shadowPadBase, shadowColor);
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

  HexView* m_view = nullptr;
  QRhi* m_rhi = nullptr;
  QRhiBuffer* m_vbuf = nullptr;
  QRhiBuffer* m_ibuf = nullptr;
  QRhiBuffer* m_baseRectBuf = nullptr;
  QRhiBuffer* m_baseGlyphBuf = nullptr;
  QRhiBuffer* m_overlayRectBuf = nullptr;
  QRhiBuffer* m_selectionRectBuf = nullptr;
  QRhiBuffer* m_selectionGlyphBuf = nullptr;
  QRhiBuffer* m_shadowBuf = nullptr;
  QRhiBuffer* m_ubuf = nullptr;
  QRhiBuffer* m_shadowUbuf = nullptr;
  QRhiTexture* m_glyphTex = nullptr;
  QRhiSampler* m_glyphSampler = nullptr;
  QRhiShaderResourceBindings* m_rectSrb = nullptr;
  QRhiShaderResourceBindings* m_glyphSrb = nullptr;
  QRhiShaderResourceBindings* m_shadowSrb = nullptr;
  QRhiGraphicsPipeline* m_rectPso = nullptr;
  QRhiGraphicsPipeline* m_glyphPso = nullptr;
  QRhiGraphicsPipeline* m_shadowPso = nullptr;
  int m_sampleCount = 1;
  uint64_t m_glyphAtlasVersion = 0;

  std::vector<CachedLine> m_cachedLines;
  std::vector<RectInstance> m_baseRectInstances;
  std::vector<GlyphInstance> m_baseGlyphInstances;
  std::vector<RectInstance> m_overlayRectInstances;
  std::vector<RectInstance> m_selectionRectInstances;
  std::vector<GlyphInstance> m_selectionGlyphInstances;
  std::vector<ShadowInstance> m_shadowInstances;
  int m_cacheStartLine = 0;
  int m_cacheEndLine = -1;
  int m_lastStartLine = -1;
  int m_lastEndLine = -1;
  int m_lastOverlayScrollY = std::numeric_limits<int>::min();
  bool m_baseDirty = true;
  bool m_selectionDirty = true;
  bool m_overlayDirty = true;
  bool m_baseBufferDirty = false;
  bool m_selectionBufferDirty = false;
  bool m_overlayBufferDirty = false;
};

HexView::HexView(VGMFile* vgmfile, QWidget* parent)
    : QAbstractScrollArea(parent), m_vgmfile(vgmfile) {
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setAutoFillBackground(false);

  m_rhiViewport = new HexViewViewport(this);
  m_rhiViewport->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_rhiViewport->setParent(viewport());
  m_rhiViewport->setGeometry(viewport()->rect());
  m_rhiViewport->show();

  const double appFontPointSize = QApplication::font().pointSizeF();
  QFont font("Roboto Mono", appFontPointSize + 1.0);
  font.setPointSizeF(appFontPointSize + 1.0);

  setFont(font);
  rebuildStyleMap();
  initAnimations();
  updateLayout();
}

void HexView::setFont(const QFont& font) {
  QFont adjustedFont = font;
  QFontMetricsF fontMetrics(adjustedFont);

  const qreal originalCharWidth = fontMetrics.horizontalAdvance("A");
  const qreal originalLetterSpacing = adjustedFont.letterSpacing();
  const qreal charWidthSansSpacing = originalCharWidth - originalLetterSpacing;
  const qreal targetLetterSpacing = std::round(charWidthSansSpacing) - charWidthSansSpacing;
  adjustedFont.setLetterSpacing(QFont::AbsoluteSpacing, targetLetterSpacing);

  fontMetrics = QFontMetricsF(adjustedFont);
  m_charWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A")));
  m_charHalfWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A") / 2.0));
  m_lineHeight = static_cast<int>(std::round(fontMetrics.height()));

  QAbstractScrollArea::setFont(adjustedFont);

  m_virtual_full_width = -1;
  m_virtual_width_sans_ascii = -1;
  m_virtual_width_sans_ascii_and_address = -1;

  if (m_glyphAtlas) {
    m_glyphAtlas->dpr = 0.0;
  }

  updateLayout();
  if (m_rhiViewport) {
    m_rhiViewport->markBaseDirty();
    m_rhiViewport->markSelectionDirty();
    m_rhiViewport->markOverlayDirty();
    m_rhiViewport->update();
  }
}

int HexView::hexXOffset() const {
  return m_shouldDrawOffset ? ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * m_charWidth) : 0;
}

int HexView::getVirtualHeight() const {
  return m_lineHeight * getTotalLines();
}

int HexView::getVirtualFullWidth() const {
  if (m_virtual_full_width == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3) +
                             HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE;
    m_virtual_full_width = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_full_width;
}

int HexView::getVirtualWidthSansAscii() const {
  if (m_virtual_width_sans_ascii == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3);
    m_virtual_width_sans_ascii = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii;
}

int HexView::getVirtualWidthSansAsciiAndAddress() const {
  if (m_virtual_width_sans_ascii_and_address == -1) {
    constexpr int numChars = BYTES_PER_LINE * 3;
    m_virtual_width_sans_ascii_and_address = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii_and_address;
}

int HexView::getActualVirtualWidth() const {
  if (m_shouldDrawAscii) {
    return getVirtualFullWidth();
  }
  if (m_shouldDrawOffset) {
    return getVirtualWidthSansAscii();
  }
  return getVirtualWidthSansAsciiAndAddress();
}

int HexView::getViewportFullWidth() const {
  return getVirtualFullWidth() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAscii() const {
  return getVirtualWidthSansAscii() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAsciiAndAddress() const {
  return getVirtualWidthSansAsciiAndAddress() + VIEWPORT_PADDING;
}

void HexView::updateScrollBars() {
  const int totalHeight = getVirtualHeight();
  const int pageStep = viewport()->height();
  verticalScrollBar()->setRange(0, std::max(0, totalHeight - pageStep));
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(m_lineHeight);
}

void HexView::updateLayout() {
  const int width = viewport()->width();
  const int height = viewport()->height();
  if (width == 0 || height == 0) {
    return;
  }

  const bool newDrawOffset = width >= getViewportWidthSansAsciiAndAddress();
  const bool newDrawAscii = width >= getViewportWidthSansAscii();
  const bool offsetChanged = (newDrawOffset != m_shouldDrawOffset);
  const bool asciiChanged = (newDrawAscii != m_shouldDrawAscii);
  m_shouldDrawOffset = newDrawOffset;
  m_shouldDrawAscii = newDrawAscii;

  updateScrollBars();

  if (m_rhiViewport) {
    if (offsetChanged || asciiChanged) {
      m_rhiViewport->markBaseDirty();
      m_rhiViewport->markSelectionDirty();
    }
    m_rhiViewport->markOverlayDirty();
    m_rhiViewport->setGeometry(viewport()->rect());
    m_rhiViewport->update();
  }
}

int HexView::getTotalLines() const {
  if (!m_vgmfile) {
    return 0;
  }
  return static_cast<int>((m_vgmfile->unLength + BYTES_PER_LINE - 1) / BYTES_PER_LINE);
}

void HexView::setSelectedItem(VGMItem* item) {
  m_selectedItem = item;

  if (!m_selectedItem) {
    if (!m_selections.empty()) {
      m_fadeSelections = m_selections;
    }
    m_selections.clear();
    showSelectedItem(false, true);
    if (m_rhiViewport) {
      m_rhiViewport->markSelectionDirty();
      m_rhiViewport->update();
    }
    return;
  }

  m_selectedOffset = m_selectedItem->dwOffset;
  m_selections.clear();
  m_selections.push_back({m_selectedItem->dwOffset, m_selectedItem->unLength});
  m_fadeSelections.clear();

  showSelectedItem(true, true);

  if (m_rhiViewport) {
    m_rhiViewport->markSelectionDirty();
    m_rhiViewport->update();
  }

  if (!m_lineHeight) {
    return;
  }

  const int itemBaseOffset = static_cast<int>(m_selectedItem->dwOffset - m_vgmfile->dwOffset);
  const int line = itemBaseOffset / BYTES_PER_LINE;
  const int endLine = (itemBaseOffset + static_cast<int>(m_selectedItem->unLength)) / BYTES_PER_LINE;

  const int viewStartLine = verticalScrollBar()->value() / m_lineHeight;
  const int viewEndLine = viewStartLine + (viewport()->height() / m_lineHeight);

  if (line <= viewEndLine && endLine > viewStartLine) {
    return;
  }

  if (line < viewStartLine) {
    verticalScrollBar()->setValue(line * m_lineHeight);
  } else if (endLine > viewEndLine) {
    if ((endLine - line) > (viewport()->height() / m_lineHeight)) {
      verticalScrollBar()->setValue(line * m_lineHeight);
    } else {
      const int y = ((endLine + 1) * m_lineHeight) + 1 - viewport()->height();
      verticalScrollBar()->setValue(y);
    }
  }
}

void HexView::rebuildStyleMap() {
  m_styles.clear();
  m_typeToStyleId.clear();

  Style defaultStyle;
  defaultStyle.bg = palette().color(QPalette::Window);
  defaultStyle.fg = palette().color(QPalette::WindowText);
  m_styles.push_back(defaultStyle);

  if (!m_vgmfile) {
    return;
  }

  const uint32_t length = m_vgmfile->unLength;
  m_styleIds.assign(length, STYLE_UNASSIGNED);

  auto styleIdForType = [&](VGMItem::Type type) -> uint16_t {
    const int key = static_cast<int>(type);
    auto it = m_typeToStyleId.find(key);
    if (it != m_typeToStyleId.end()) {
      return it->second;
    }
    Style style;
    style.bg = colorForItemType(type);
    style.fg = textColorForItemType(type);
    uint16_t id = static_cast<uint16_t>(m_styles.size());
    m_styles.push_back(style);
    m_typeToStyleId.emplace(key, id);
    return id;
  };

  std::vector<VGMItem*> leaves;
  std::function<void(VGMItem*)> walk = [&](VGMItem* item) {
    auto& children = item->children();
    if (children.empty()) {
      leaves.push_back(item);
      return;
    }
    for (auto* child : children) {
      walk(child);
    }
  };

  walk(m_vgmfile);

  for (auto* item : leaves) {
    if (!item || item->unLength == 0) {
      continue;
    }
    if (item->dwOffset < m_vgmfile->dwOffset) {
      continue;
    }
    const uint32_t start = item->dwOffset - m_vgmfile->dwOffset;
    if (start >= length) {
      continue;
    }
    const uint32_t end = std::min<uint32_t>(start + item->unLength, length);
    const uint16_t styleId = styleIdForType(item->type);
    for (uint32_t i = start; i < end; ++i) {
      if (m_styleIds[i] == STYLE_UNASSIGNED) {
        m_styleIds[i] = styleId;
      }
    }
  }

  for (auto& styleId : m_styleIds) {
    if (styleId == STYLE_UNASSIGNED) {
      styleId = 0;
    }
  }

  if (m_rhiViewport) {
    m_rhiViewport->invalidateCache();
  }
}

void HexView::ensureGlyphAtlas(qreal dpr) {
  if (!m_glyphAtlas) {
    m_glyphAtlas = std::make_unique<GlyphAtlas>();
  }

  const bool needsRebuild =
      m_glyphAtlas->dpr != dpr ||
      m_glyphAtlas->glyphWidth != m_charWidth ||
      m_glyphAtlas->glyphHeight != m_lineHeight ||
      m_glyphAtlas->font != font();

  if (!needsRebuild) {
    return;
  }

  const int padding = 1;
  const int glyphWidth = m_charWidth;
  const int glyphHeight = m_lineHeight;
  const int cellWidth = glyphWidth + padding * 2;
  const int cellHeight = glyphHeight + padding * 2;

  std::vector<QChar> glyphs;
  glyphs.reserve(0x7E - 0x20 + 1);
  for (ushort ch = 0x20; ch <= 0x7E; ++ch) {
    glyphs.emplace_back(QChar(ch));
  }

  const int columns = 16;
  const int rows = static_cast<int>((glyphs.size() + columns - 1) / columns);

  const int imageWidth = static_cast<int>(std::ceil(cellWidth * dpr * columns));
  const int imageHeight = static_cast<int>(std::ceil(cellHeight * dpr * rows));

  QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  image.setDevicePixelRatio(dpr);

  QPainter painter(&image);
  painter.setFont(font());
  painter.setPen(Qt::white);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const qreal baseline = QFontMetricsF(font()).ascent();

  m_glyphAtlas->uvTable.fill(QRectF());

  for (size_t i = 0; i < glyphs.size(); ++i) {
    const int col = static_cast<int>(i % columns);
    const int row = static_cast<int>(i / columns);
    const qreal cellX = col * cellWidth;
    const qreal cellY = row * cellHeight;

    painter.drawText(QPointF(cellX + padding, cellY + padding + baseline), QString(glyphs[i]));

    const qreal u0 = (cellX + padding) * dpr / imageWidth;
    const qreal v0 = (cellY + padding) * dpr / imageHeight;
    const qreal u1 = (cellX + padding + glyphWidth) * dpr / imageWidth;
    const qreal v1 = (cellY + padding + glyphHeight) * dpr / imageHeight;

    const ushort code = glyphs[i].unicode();
    if (code < m_glyphAtlas->uvTable.size()) {
      m_glyphAtlas->uvTable[code] = QRectF(u0, v0, u1 - u0, v1 - v0);
    }
  }

  m_glyphAtlas->image = std::move(image);
  m_glyphAtlas->dpr = dpr;
  m_glyphAtlas->glyphWidth = glyphWidth;
  m_glyphAtlas->glyphHeight = glyphHeight;
  m_glyphAtlas->cellWidth = cellWidth;
  m_glyphAtlas->cellHeight = cellHeight;
  m_glyphAtlas->font = font();
  m_glyphAtlas->version++;

  if (m_rhiViewport) {
    m_rhiViewport->markBaseDirty();
    m_rhiViewport->markSelectionDirty();
  }
}

bool HexView::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    auto* helpEvent = static_cast<QHelpEvent*>(event);
    const int offset = getOffsetFromPoint(helpEvent->pos());
    if (offset < 0) {
      QToolTip::hideText();
      return true;
    }
    if (VGMItem* item = m_vgmfile->getItemAtOffset(offset, false)) {
      const QString description = getFullDescriptionForTooltip(item);
      if (!description.isEmpty()) {
        QToolTip::showText(helpEvent->globalPos(), description, this);
      }
    }
    return true;
  }

  return QAbstractScrollArea::viewportEvent(event);
}

void HexView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  updateLayout();
}

void HexView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  if (m_rhiViewport) {
    m_rhiViewport->update();
  }
}

void HexView::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange) {
    if (!m_styles.empty()) {
      m_styles[0].bg = palette().color(QPalette::Window);
      m_styles[0].fg = palette().color(QPalette::WindowText);
    }
    if (m_rhiViewport) {
      m_rhiViewport->markBaseDirty();
      m_rhiViewport->markSelectionDirty();
      m_rhiViewport->update();
    }
  }
  QAbstractScrollArea::changeEvent(event);
}

void HexView::keyPressEvent(QKeyEvent* event) {
  if (!m_selectedItem) {
    QAbstractScrollArea::keyPressEvent(event);
    return;
  }

  uint32_t newOffset = 0;
  switch (event->key()) {
    case Qt::Key_Up:
      newOffset = m_selectedOffset - BYTES_PER_LINE;
      goto selectNewOffset;

    case Qt::Key_Down: {
      const int selectedCol = (m_selectedOffset - m_vgmfile->dwOffset) % BYTES_PER_LINE;
      const int endOffset = m_selectedItem->dwOffset - m_vgmfile->dwOffset + m_selectedItem->unLength;
      const int itemEndCol = endOffset % BYTES_PER_LINE;
      const int itemEndLine = endOffset / BYTES_PER_LINE;
      newOffset = m_vgmfile->dwOffset +
                  ((itemEndLine + (selectedCol > itemEndCol ? 0 : 1)) * BYTES_PER_LINE) +
                  selectedCol;
      goto selectNewOffset;
    }

    case Qt::Key_Left:
      newOffset = m_selectedItem->dwOffset - 1;
      goto selectNewOffset;

    case Qt::Key_Right:
      newOffset = m_selectedItem->dwOffset + m_selectedItem->unLength;
      goto selectNewOffset;

    case Qt::Key_Escape:
      selectionChanged(nullptr);
      return;

    selectNewOffset:
      if (newOffset >= m_vgmfile->dwOffset &&
          newOffset < (m_vgmfile->dwOffset + m_vgmfile->unLength)) {
        m_selectedOffset = newOffset;
        if (auto* item = m_vgmfile->getItemAtOffset(newOffset, false)) {
          selectionChanged(item);
        }
      }
      return;

    default:
      QAbstractScrollArea::keyPressEvent(event);
      return;
  }
}

int HexView::getOffsetFromPoint(QPoint pos) const {
  const int y = pos.y() + verticalScrollBar()->value();
  const int line = m_lineHeight > 0 ? (y / m_lineHeight) : 0;
  if (line < 0 || line >= getTotalLines()) {
    return -1;
  }

  const int hexStart = hexXOffset();
  const int hexEnd = hexStart + (BYTES_PER_LINE * 3 * m_charWidth);
  const int asciiStart = hexEnd + (HEX_TO_ASCII_SPACING_CHARS * m_charWidth);
  const int asciiEnd = asciiStart + (BYTES_PER_LINE * m_charWidth);

  int byteNum = -1;
  if (pos.x() >= hexStart - m_charHalfWidth && pos.x() < hexEnd - m_charHalfWidth) {
    byteNum = ((pos.x() - hexStart + m_charHalfWidth) / m_charWidth) / 3;
  } else if (pos.x() >= asciiStart && pos.x() < asciiEnd) {
    byteNum = (pos.x() - asciiStart) / m_charWidth;
  }
  if (byteNum == -1) {
    return -1;
  }

  const int offset = m_vgmfile->dwOffset + (line * BYTES_PER_LINE) + byteNum;
  if (offset < static_cast<int>(m_vgmfile->dwOffset) ||
      offset >= static_cast<int>(m_vgmfile->dwOffset + m_vgmfile->unLength)) {
    return -1;
  }
  return offset;
}

void HexView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const int offset = getOffsetFromPoint(event->pos());
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }

    m_selectedOffset = offset;
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    if (item == m_selectedItem) {
      selectionChanged(nullptr);
    } else {
      selectionChanged(item);
    }
    m_isDragging = true;
  }

  QAbstractScrollArea::mousePressEvent(event);
}

void HexView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void HexView::mouseMoveEvent(QMouseEvent* event) {
  if (m_isDragging && event->buttons() & Qt::LeftButton) {
    const int offset = getOffsetFromPoint(event->pos());
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }
    m_selectedOffset = offset;
    if (m_selectedItem && (m_selectedOffset >= m_selectedItem->dwOffset) &&
        (m_selectedOffset < (m_selectedItem->dwOffset + m_selectedItem->unLength))) {
      return;
    }
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    if (item != m_selectedItem) {
      selectionChanged(item);
    }
  }
  QAbstractScrollArea::mouseMoveEvent(event);
}

void HexView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (m_shouldDrawOffset && event->pos().x() >= 0 &&
        event->pos().x() < (NUM_ADDRESS_NIBBLES * m_charWidth)) {
      m_addressAsHex = !m_addressAsHex;
      if (m_rhiViewport) {
        m_rhiViewport->markBaseDirty();
        m_rhiViewport->update();
      }
    }
  }
  QAbstractScrollArea::mouseDoubleClickEvent(event);
}

qreal HexView::overlayOpacity() const {
  return m_overlayOpacity;
}

void HexView::setOverlayOpacity(qreal opacity) {
  if (qFuzzyCompare(opacity, m_overlayOpacity)) {
    return;
  }
  m_overlayOpacity = opacity;
  if (m_rhiViewport) {
    m_rhiViewport->markOverlayDirty();
    m_rhiViewport->update();
  }
}

qreal HexView::shadowBlur() const {
  return m_shadowBlur;
}

void HexView::setShadowBlur(qreal blur) {
  if (qFuzzyCompare(blur, m_shadowBlur)) {
    return;
  }
  m_shadowBlur = blur;
  if (m_rhiViewport) {
    m_rhiViewport->update();
  }
}

QPointF HexView::shadowOffset() const {
  return m_shadowOffset;
}

void HexView::setShadowOffset(const QPointF& offset) {
  if (offset == m_shadowOffset) {
    return;
  }
  m_shadowOffset = offset;
  if (m_rhiViewport) {
    m_rhiViewport->update();
  }
}

void HexView::initAnimations() {
  m_selectionAnimation = new QParallelAnimationGroup(this);

  auto* overlayOpacityAnimation = new QPropertyAnimation(this, "overlayOpacity");
  overlayOpacityAnimation->setDuration(DIM_DURATION_MS);
  overlayOpacityAnimation->setStartValue(0.0);
  overlayOpacityAnimation->setEndValue(OVERLAY_ALPHA_F);
  overlayOpacityAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto* shadowBlurAnimation = new QPropertyAnimation(this, "shadowBlur");
  shadowBlurAnimation->setDuration(DIM_DURATION_MS);
  shadowBlurAnimation->setStartValue(0.0);
  shadowBlurAnimation->setEndValue(SHADOW_BLUR_RADIUS);
  shadowBlurAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto* shadowOffsetAnimation = new QPropertyAnimation(this, "shadowOffset");
  shadowOffsetAnimation->setDuration(DIM_DURATION_MS);
  shadowOffsetAnimation->setStartValue(QPointF(0.0, 0.0));
  shadowOffsetAnimation->setEndValue(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
  shadowOffsetAnimation->setEasingCurve(QEasingCurve::OutQuad);

  m_selectionAnimation->addAnimation(overlayOpacityAnimation);
  m_selectionAnimation->addAnimation(shadowBlurAnimation);
  m_selectionAnimation->addAnimation(shadowOffsetAnimation);

  connect(m_selectionAnimation, &QParallelAnimationGroup::finished, this,
          [this, overlayOpacityAnimation, shadowBlurAnimation, shadowOffsetAnimation]() {
            if (m_selectionAnimation->direction() == QAbstractAnimation::Backward) {
              clearFadeSelection();
            }
            const auto quadCurve = m_selectionAnimation->direction() == QAbstractAnimation::Forward
                                       ? QEasingCurve::InQuad
                                       : QEasingCurve::OutQuad;
            const auto expoCurve = m_selectionAnimation->direction() == QAbstractAnimation::Forward
                                       ? QEasingCurve::InExpo
                                       : QEasingCurve::OutExpo;
            overlayOpacityAnimation->setEasingCurve(quadCurve);
            shadowBlurAnimation->setEasingCurve(quadCurve);
            shadowOffsetAnimation->setEasingCurve(expoCurve);
          });
}

void HexView::showSelectedItem(bool show, bool animate) {
  if (!animate) {
    m_selectionAnimation->stop();
    if (show) {
      setOverlayOpacity(OVERLAY_ALPHA_F);
      setShadowBlur(SHADOW_BLUR_RADIUS);
      setShadowOffset(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
    } else {
      setOverlayOpacity(0.0);
      setShadowBlur(0.0);
      setShadowOffset(QPointF(0.0, 0.0));
      clearFadeSelection();
    }
    return;
  }

  if (show) {
    if (m_selectionAnimation->state() == QAbstractAnimation::Stopped && m_overlayOpacity > 0.0) {
      return;
    }
    if (m_selectionAnimation->state() == QAbstractAnimation::Running &&
        m_selectionAnimation->direction() == QAbstractAnimation::Forward) {
      return;
    }
    if (m_selectionAnimation->state() == QAbstractAnimation::Running) {
      m_selectionAnimation->pause();
    }
    m_selectionAnimation->setDirection(QAbstractAnimation::Forward);
    if (m_selectionAnimation->state() == QAbstractAnimation::Paused) {
      m_selectionAnimation->resume();
    } else {
      m_selectionAnimation->start();
    }
  } else {
    if (m_selectionAnimation->state() == QAbstractAnimation::Running) {
      m_selectionAnimation->pause();
    }
    m_selectionAnimation->setDirection(QAbstractAnimation::Backward);
    if (m_selectionAnimation->state() == QAbstractAnimation::Paused) {
      m_selectionAnimation->resume();
    } else {
      m_selectionAnimation->start();
    }
  }
}

void HexView::clearFadeSelection() {
  m_fadeSelections.clear();
  if (m_rhiViewport) {
    m_rhiViewport->markSelectionDirty();
    m_rhiViewport->update();
  }
}
