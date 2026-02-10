/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollRhiRenderer.h"

#include "PianoRollView.h"

#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <QColor>
#include <QFile>
#include <QMatrix4x4>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iterator>

namespace {

static const float kVertices[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
};

static const quint16 kIndices[] = {0, 1, 2, 0, 2, 3};

static constexpr int kMat4Bytes = 16 * sizeof(float);
static constexpr int kUniformBytes = (16 + 4) * sizeof(float);
static constexpr float kDashSegmentPx = 4.0f;
static constexpr float kDashGapPx = 4.0f;
static constexpr int kTileLogicalWidth = 1024;
static constexpr int kTileLogicalHeight = 512;
static constexpr int kTilePrefetchMargin = 1;

QShader loadShader(const char* path) {
  QFile file(QString::fromUtf8(path));
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning("Failed to load shader: %s", path);
    return {};
  }
  return QShader::fromSerialized(file.readAll());
}

}  // namespace

PianoRollRhiRenderer::PianoRollRhiRenderer(PianoRollView* view)
    : m_view(view) {
}

PianoRollRhiRenderer::~PianoRollRhiRenderer() {
  releaseResources();
}

void PianoRollRhiRenderer::initIfNeeded(QRhi* rhi) {
  if (m_inited || !rhi) {
    return;
  }

  m_rhi = rhi;

  m_vertexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable,
                                    QRhiBuffer::VertexBuffer,
                                    static_cast<int>(sizeof(kVertices)));
  m_vertexBuffer->create();

  m_indexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable,
                                   QRhiBuffer::IndexBuffer,
                                   static_cast<int>(sizeof(kIndices)));
  m_indexBuffer->create();

  const int ubufSize = m_rhi->ubufAligned(kUniformBytes);
  m_uniformBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, ubufSize);
  m_uniformBuffer->create();
  m_tileUniformBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, ubufSize);
  m_tileUniformBuffer->create();

  m_geometryUploaded = false;
  m_staticInstanceBuffersDirty = true;
  m_staticGeneration = 1;
  m_inited = true;
}

void PianoRollRhiRenderer::releaseResources() {
  if (m_rhi) {
    m_rhi->makeThreadLocalNativeContextCurrent();
  }

  delete m_outputRectPipeline;
  m_outputRectPipeline = nullptr;
  delete m_tileRectPipeline;
  m_tileRectPipeline = nullptr;
  delete m_tileCompositePipeline;
  m_tileCompositePipeline = nullptr;

  delete m_outputRectSrb;
  m_outputRectSrb = nullptr;
  delete m_tileRectSrb;
  m_tileRectSrb = nullptr;
  delete m_tileCompositeLayoutSrb;
  m_tileCompositeLayoutSrb = nullptr;

  releaseTileCaches();

  delete m_tileDummyTexture;
  m_tileDummyTexture = nullptr;
  delete m_tileSampler;
  m_tileSampler = nullptr;

  delete m_staticFixedInstanceBuffer;
  m_staticFixedInstanceBuffer = nullptr;
  delete m_staticXyInstanceBuffer;
  m_staticXyInstanceBuffer = nullptr;
  delete m_staticXInstanceBuffer;
  m_staticXInstanceBuffer = nullptr;
  delete m_staticYInstanceBuffer;
  m_staticYInstanceBuffer = nullptr;

  delete m_dynamicInstanceBuffer;
  m_dynamicInstanceBuffer = nullptr;
  delete m_tileInstanceBuffer;
  m_tileInstanceBuffer = nullptr;

  delete m_uniformBuffer;
  m_uniformBuffer = nullptr;
  delete m_tileUniformBuffer;
  m_tileUniformBuffer = nullptr;

  delete m_indexBuffer;
  m_indexBuffer = nullptr;

  delete m_vertexBuffer;
  m_vertexBuffer = nullptr;

  m_outputRenderPass = nullptr;
  m_tileRenderPass = nullptr;
  m_sampleCount = 1;
  m_tilePixelSize = {};
  m_staticGeneration = 0;
  m_geometryUploaded = false;
  m_staticInstanceBuffersDirty = true;
  m_inited = false;
  m_staticCacheKey = {};
  m_staticFixedInstances.clear();
  for (auto& instances : m_staticPlaneInstanceData) {
    instances.clear();
  }
  m_dynamicInstances.clear();
  m_visibleTileInstances.clear();
  m_visibleTileSrbs.clear();
  m_rhi = nullptr;
}

void PianoRollRhiRenderer::renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target) {
  if (!cb || !m_rhi || !m_view || !target.renderTarget || !target.renderPassDesc || target.pixelSize.isEmpty()) {
    return;
  }

  const PianoRollFrame::Data frame = m_view->captureRhiFrameData(target.dpr);
  if (frame.viewportSize.isEmpty()) {
    return;
  }

  QSize logicalSize = frame.viewportSize;
  if (logicalSize.isEmpty()) {
    const float dpr = std::max(1.0f, target.dpr);
    logicalSize = QSize(std::max(1, static_cast<int>(std::lround(static_cast<float>(target.pixelSize.width()) / dpr))),
                        std::max(1, static_cast<int>(std::lround(static_cast<float>(target.pixelSize.height()) / dpr))));
  }

  const Layout layout = computeLayout(frame, logicalSize);
  const uint64_t trackColorsHash = hashTrackColors(frame.trackColors);
  const StaticCacheKey key = makeStaticCacheKey(frame, layout, trackColorsHash);
  if (!m_staticCacheKey.valid || !staticCacheKeyEquals(m_staticCacheKey, key)) {
    buildStaticInstances(frame, layout, trackColorsHash);
    m_staticCacheKey = key;
    m_staticInstanceBuffersDirty = true;
    ++m_staticGeneration;
    releaseTileCaches();
  }
  buildDynamicInstances(frame, layout);

  updateVisibleTiles(layout, target.dpr);
  ensurePipelines(target.renderPassDesc, std::max(1, target.sampleCount));

  QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
  if (!m_geometryUploaded) {
    updates->uploadStaticBuffer(m_vertexBuffer, kVertices);
    updates->uploadStaticBuffer(m_indexBuffer, kIndices);
    m_geometryUploaded = true;
  }

  if (m_staticInstanceBuffersDirty) {
    const int fixedBytes = static_cast<int>(m_staticFixedInstances.size() * sizeof(RectInstance));
    if (fixedBytes > 0 && ensureInstanceBuffer(m_staticFixedInstanceBuffer, fixedBytes, 8192)) {
      updates->updateDynamicBuffer(m_staticFixedInstanceBuffer, 0, fixedBytes, m_staticFixedInstances.data());
    }

    for (int i = 0; i < static_cast<int>(StaticPlane::Count); ++i) {
      const auto plane = static_cast<StaticPlane>(i);
      auto& instances = m_staticPlaneInstanceData[static_cast<size_t>(i)];
      QRhiBuffer*& planeBuffer = staticPlaneBuffer(plane);
      const int bytes = static_cast<int>(instances.size() * sizeof(RectInstance));
      if (bytes > 0 && ensureInstanceBuffer(planeBuffer, bytes, 16384)) {
        updates->updateDynamicBuffer(planeBuffer, 0, bytes, instances.data());
      }
    }

    m_staticInstanceBuffersDirty = false;
  }

  if (!m_dynamicInstances.empty()) {
    const int dynamicBytes = static_cast<int>(m_dynamicInstances.size() * sizeof(RectInstance));
    if (ensureInstanceBuffer(m_dynamicInstanceBuffer, dynamicBytes, 8192)) {
      updates->updateDynamicBuffer(m_dynamicInstanceBuffer, 0, dynamicBytes, m_dynamicInstances.data());
    }
  }

  if (!m_visibleTileInstances.empty()) {
    const int tileBytes = static_cast<int>(m_visibleTileInstances.size() * sizeof(TileInstance));
    if (ensureInstanceBuffer(m_tileInstanceBuffer, tileBytes, 4096)) {
      updates->updateDynamicBuffer(m_tileInstanceBuffer, 0, tileBytes, m_visibleTileInstances.data());
    }
  }

  QMatrix4x4 outputMvp;
  outputMvp.ortho(0.0f,
                  static_cast<float>(logicalSize.width()),
                  static_cast<float>(logicalSize.height()),
                  0.0f,
                  -1.0f,
                  1.0f);
  std::array<float, 20> outputUbo{};
  fillUniformData(outputUbo, outputMvp, layout.scrollX, layout.scrollY);
  updates->updateDynamicBuffer(m_uniformBuffer, 0, kUniformBytes, outputUbo.data());

  renderMissingTiles(cb, updates);

  cb->beginPass(target.renderTarget, frame.backgroundColor, {1.0f, 0}, updates);
  cb->setViewport(QRhiViewport(0,
                               0,
                               static_cast<float>(target.pixelSize.width()),
                               static_cast<float>(target.pixelSize.height())));

  if (m_outputRectPipeline && m_outputRectSrb) {
    cb->setGraphicsPipeline(m_outputRectPipeline);
    cb->setShaderResources(m_outputRectSrb);

    auto drawInstances = [&](QRhiBuffer* buffer, int count) {
      if (!buffer || count <= 0) {
        return;
      }
      const QRhiCommandBuffer::VertexInput bindings[] = {
          {m_vertexBuffer, 0},
          {buffer, 0},
      };
      cb->setVertexInput(0,
                         static_cast<int>(std::size(bindings)),
                         bindings,
                         m_indexBuffer,
                         0,
                         QRhiCommandBuffer::IndexUInt16);
      cb->drawIndexed(6, count, 0, 0, 0);
    };

    drawInstances(m_staticFixedInstanceBuffer, static_cast<int>(m_staticFixedInstances.size()));

    if (m_tileCompositePipeline && m_tileInstanceBuffer && !m_visibleTileInstances.empty()) {
      cb->setGraphicsPipeline(m_tileCompositePipeline);

      const QRhiCommandBuffer::VertexInput tileBindings[] = {
          {m_vertexBuffer, 0},
          {m_tileInstanceBuffer, 0},
      };
      cb->setVertexInput(0,
                         static_cast<int>(std::size(tileBindings)),
                         tileBindings,
                         m_indexBuffer,
                         0,
                         QRhiCommandBuffer::IndexUInt16);

      for (size_t i = 0; i < m_visibleTileSrbs.size(); ++i) {
        QRhiShaderResourceBindings* srb = m_visibleTileSrbs[i];
        if (!srb) {
          continue;
        }
        cb->setShaderResources(srb);
        cb->drawIndexed(6, 1, 0, 0, static_cast<int>(i));
      }

      cb->setGraphicsPipeline(m_outputRectPipeline);
      cb->setShaderResources(m_outputRectSrb);
    }

    drawInstances(m_dynamicInstanceBuffer, static_cast<int>(m_dynamicInstances.size()));
  }

  cb->endPass();
}

bool PianoRollRhiRenderer::isBlackKey(int key) {
  switch (key % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      return true;
    default:
      return false;
  }
}

uint64_t PianoRollRhiRenderer::hashTrackColors(const std::vector<QColor>& colors) {
  uint64_t hash = 1469598103934665603ULL;
  for (const QColor& color : colors) {
    hash ^= static_cast<uint64_t>(color.rgba());
    hash *= 1099511628211ULL;
  }
  hash ^= static_cast<uint64_t>(colors.size());
  hash *= 1099511628211ULL;
  return hash;
}

uint32_t PianoRollRhiRenderer::colorKey(const QColor& color) {
  return color.rgba();
}

int PianoRollRhiRenderer::planeIndex(StaticPlane plane) {
  return static_cast<int>(plane);
}

uint64_t PianoRollRhiRenderer::makeTileKey(int tileX, int tileY) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(tileY)) << 32) |
         static_cast<uint64_t>(static_cast<uint32_t>(tileX));
}

void PianoRollRhiRenderer::fillUniformData(std::array<float, 20>& ubo,
                                           const QMatrix4x4& mvp,
                                           float cameraX,
                                           float cameraY) {
  std::memcpy(ubo.data(), mvp.constData(), kMat4Bytes);
  ubo[16] = cameraX;
  ubo[17] = cameraY;
  ubo[18] = 0.0f;
  ubo[19] = 0.0f;
}

void PianoRollRhiRenderer::ensurePipelines(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount) {
  if (!renderPassDesc) {
    return;
  }

  const bool needCompositePipeline = !m_visibleTileSrbs.empty();
  const bool outputConfigChanged = (m_outputRenderPass != renderPassDesc) || (m_sampleCount != sampleCount);
  if (outputConfigChanged) {
    delete m_outputRectPipeline;
    m_outputRectPipeline = nullptr;
    delete m_tileCompositePipeline;
    m_tileCompositePipeline = nullptr;
    delete m_outputRectSrb;
    m_outputRectSrb = nullptr;
    m_outputRenderPass = renderPassDesc;
    m_sampleCount = sampleCount;
  }

  if (!m_outputRectSrb) {
    m_outputRectSrb = m_rhi->newShaderResourceBindings();
    m_outputRectSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage,
                                                 m_uniformBuffer),
    });
    m_outputRectSrb->create();
  }

  if (!m_tileRectSrb) {
    m_tileRectSrb = m_rhi->newShaderResourceBindings();
    m_tileRectSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage,
                                                 m_tileUniformBuffer),
    });
    m_tileRectSrb->create();
  }

  if (!m_outputRectPipeline) {
    QShader vertexShader = loadShader(":/shaders/pianorollquad.vert.qsb");
    QShader fragmentShader = loadShader(":/shaders/pianorollquad.frag.qsb");

    QRhiVertexInputLayout rectInputLayout;
    rectInputLayout.setBindings({
        {2 * static_cast<int>(sizeof(float))},
        {static_cast<int>(sizeof(RectInstance)), QRhiVertexInputBinding::PerInstance},
    });
    rectInputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {1, 1, QRhiVertexInputAttribute::Float4, 0},
        {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
        {1, 3, QRhiVertexInputAttribute::Float4, 8 * static_cast<int>(sizeof(float))},
        {1, 4, QRhiVertexInputAttribute::Float2, 12 * static_cast<int>(sizeof(float))},
    });

    m_outputRectPipeline = m_rhi->newGraphicsPipeline();
    m_outputRectPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_outputRectPipeline->setSampleCount(m_sampleCount);
    m_outputRectPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertexShader},
        {QRhiShaderStage::Fragment, fragmentShader},
    });
    m_outputRectPipeline->setShaderResourceBindings(m_outputRectSrb);
    m_outputRectPipeline->setVertexInputLayout(rectInputLayout);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_outputRectPipeline->setTargetBlends({blend});
    m_outputRectPipeline->setRenderPassDescriptor(renderPassDesc);
    m_outputRectPipeline->create();
  }

  if (needCompositePipeline && !m_tileCompositePipeline) {
    if (!m_tileSampler) {
      m_tileSampler = m_rhi->newSampler(QRhiSampler::Linear,
                                        QRhiSampler::Linear,
                                        QRhiSampler::None,
                                        QRhiSampler::ClampToEdge,
                                        QRhiSampler::ClampToEdge);
      m_tileSampler->create();
    }
    if (!m_tileDummyTexture) {
      m_tileDummyTexture = m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
      m_tileDummyTexture->create();
    }
    if (!m_tileCompositeLayoutSrb) {
      m_tileCompositeLayoutSrb = m_rhi->newShaderResourceBindings();
      m_tileCompositeLayoutSrb->setBindings({
          QRhiShaderResourceBinding::uniformBuffer(0,
                                                   QRhiShaderResourceBinding::VertexStage,
                                                   m_uniformBuffer),
          QRhiShaderResourceBinding::sampledTexture(1,
                                                    QRhiShaderResourceBinding::FragmentStage,
                                                    m_tileDummyTexture,
                                                    m_tileSampler),
      });
      m_tileCompositeLayoutSrb->create();
    }

    QShader tileVertexShader = loadShader(":/shaders/pianorolltile.vert.qsb");
    QShader tileFragmentShader = loadShader(":/shaders/pianorolltile.frag.qsb");

    QRhiVertexInputLayout tileInputLayout;
    tileInputLayout.setBindings({
        {2 * static_cast<int>(sizeof(float))},
        {static_cast<int>(sizeof(TileInstance)), QRhiVertexInputBinding::PerInstance},
    });
    tileInputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {1, 1, QRhiVertexInputAttribute::Float4, 0},
        {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    m_tileCompositePipeline = m_rhi->newGraphicsPipeline();
    m_tileCompositePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_tileCompositePipeline->setSampleCount(m_sampleCount);
    m_tileCompositePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, tileVertexShader},
        {QRhiShaderStage::Fragment, tileFragmentShader},
    });
    m_tileCompositePipeline->setShaderResourceBindings(m_tileCompositeLayoutSrb);
    m_tileCompositePipeline->setVertexInputLayout(tileInputLayout);
    m_tileCompositePipeline->setTargetBlends({blend});
    m_tileCompositePipeline->setRenderPassDescriptor(renderPassDesc);
    m_tileCompositePipeline->create();
  }
}

void PianoRollRhiRenderer::ensureTilePipeline() {
  if (!m_tileRenderPass || !m_tileRectSrb) {
    return;
  }
  if (m_tileRectPipeline) {
    return;
  }

  QShader vertexShader = loadShader(":/shaders/pianorollquad.vert.qsb");
  QShader fragmentShader = loadShader(":/shaders/pianorollquad.frag.qsb");

  QRhiVertexInputLayout inputLayout;
  inputLayout.setBindings({
      {2 * static_cast<int>(sizeof(float))},
      {static_cast<int>(sizeof(RectInstance)), QRhiVertexInputBinding::PerInstance},
  });
  inputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {1, 1, QRhiVertexInputAttribute::Float4, 0},
      {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
      {1, 3, QRhiVertexInputAttribute::Float4, 8 * static_cast<int>(sizeof(float))},
      {1, 4, QRhiVertexInputAttribute::Float2, 12 * static_cast<int>(sizeof(float))},
  });

  m_tileRectPipeline = m_rhi->newGraphicsPipeline();
  m_tileRectPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
  m_tileRectPipeline->setSampleCount(1);
  m_tileRectPipeline->setShaderStages({
      {QRhiShaderStage::Vertex, vertexShader},
      {QRhiShaderStage::Fragment, fragmentShader},
  });
  m_tileRectPipeline->setShaderResourceBindings(m_tileRectSrb);
  m_tileRectPipeline->setVertexInputLayout(inputLayout);

  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::One;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  m_tileRectPipeline->setTargetBlends({blend});
  m_tileRectPipeline->setRenderPassDescriptor(m_tileRenderPass);
  m_tileRectPipeline->create();
}

bool PianoRollRhiRenderer::ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes, int minBytes) {
  if (bytes <= 0) {
    return true;
  }

  const int targetSize = std::max(bytes, minBytes);
  if (buffer && buffer->size() >= targetSize) {
    return true;
  }

  delete buffer;
  buffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, targetSize);
  return buffer->create();
}

QSize PianoRollRhiRenderer::tilePixelSizeForDpr(float dpr) const {
  const float clampedDpr = std::max(1.0f, dpr);
  return QSize(
      std::max(1, static_cast<int>(std::lround(static_cast<float>(kTileLogicalWidth) * clampedDpr))),
      std::max(1, static_cast<int>(std::lround(static_cast<float>(kTileLogicalHeight) * clampedDpr))));
}

PianoRollRhiRenderer::TileRange PianoRollRhiRenderer::tileRangeForPlane(const Layout& layout,
                                                                        StaticPlane plane,
                                                                        int margin) const {
  TileRange range;
  if (layout.viewWidth <= 0 || layout.viewHeight <= 0) {
    return range;
  }

  const bool scrollX = (plane == StaticPlane::XY || plane == StaticPlane::X);
  const bool scrollY = (plane == StaticPlane::XY || plane == StaticPlane::Y);

  const float x0 = scrollX ? layout.scrollX : 0.0f;
  const float y0 = scrollY ? layout.scrollY : 0.0f;
  const float x1 = x0 + static_cast<float>(layout.viewWidth);
  const float y1 = y0 + static_cast<float>(layout.viewHeight);

  range.minX = std::max(0, static_cast<int>(std::floor(x0 / static_cast<float>(kTileLogicalWidth))) - (scrollX ? margin : 0));
  range.maxX = std::max(range.minX, static_cast<int>(std::floor((x1 - 1.0f) / static_cast<float>(kTileLogicalWidth))) + (scrollX ? margin : 0));
  range.minY = std::max(0, static_cast<int>(std::floor(y0 / static_cast<float>(kTileLogicalHeight))) - (scrollY ? margin : 0));
  range.maxY = std::max(range.minY, static_cast<int>(std::floor((y1 - 1.0f) / static_cast<float>(kTileLogicalHeight))) + (scrollY ? margin : 0));
  range.valid = (range.maxX >= range.minX) && (range.maxY >= range.minY);
  return range;
}

void PianoRollRhiRenderer::updateVisibleTiles(const Layout& layout, float dpr) {
  m_visibleTileInstances.clear();
  m_visibleTileSrbs.clear();

  if (!m_rhi || layout.viewWidth <= 0 || layout.viewHeight <= 0) {
    return;
  }

  const QSize targetTilePixelSize = tilePixelSizeForDpr(dpr);
  if (m_tilePixelSize != targetTilePixelSize) {
    releaseTileCaches();
    m_tilePixelSize = targetTilePixelSize;
  }

  if (!m_tileSampler) {
    m_tileSampler = m_rhi->newSampler(QRhiSampler::Linear,
                                      QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge);
    m_tileSampler->create();
  }

  static constexpr std::array<StaticPlane, 3> kPlaneDrawOrder = {
      StaticPlane::Y,
      StaticPlane::X,
      StaticPlane::XY,
  };

  for (StaticPlane plane : kPlaneDrawOrder) {
    const int idx = planeIndex(plane);
    TilePlaneCache& cache = m_tilePlaneCaches[static_cast<size_t>(idx)];
    cache.visibleKeys.clear();
    if (staticPlaneInstances(plane).empty()) {
      cache.keepRange = {};
      continue;
    }
    cache.keepRange = tileRangeForPlane(layout, plane, kTilePrefetchMargin + 1);
    const TileRange visibleRange = tileRangeForPlane(layout, plane, kTilePrefetchMargin);
    if (!visibleRange.valid) {
      continue;
    }

    const bool scrollX = (plane == StaticPlane::XY || plane == StaticPlane::X);
    const bool scrollY = (plane == StaticPlane::XY || plane == StaticPlane::Y);
    for (int tileY = visibleRange.minY; tileY <= visibleRange.maxY; ++tileY) {
      for (int tileX = visibleRange.minX; tileX <= visibleRange.maxX; ++tileX) {
        if (!ensureTile(plane, tileX, tileY, dpr)) {
          continue;
        }

        const uint64_t key = makeTileKey(tileX, tileY);
        auto it = cache.tiles.find(key);
        if (it == cache.tiles.end() || !it->second.compositeSrb) {
          continue;
        }

        cache.visibleKeys.push_back(key);
        const float worldX = static_cast<float>(tileX * kTileLogicalWidth);
        const float worldY = static_cast<float>(tileY * kTileLogicalHeight);
        const float screenX = worldX - (scrollX ? layout.scrollX : 0.0f);
        const float screenY = worldY - (scrollY ? layout.scrollY : 0.0f);
        m_visibleTileInstances.push_back(TileInstance{
            screenX,
            screenY,
            static_cast<float>(kTileLogicalWidth),
            static_cast<float>(kTileLogicalHeight),
            0.0f,
            0.0f,
            1.0f,
            1.0f,
        });
        m_visibleTileSrbs.push_back(it->second.compositeSrb);
      }
    }
  }

  pruneTileCaches();
}

void PianoRollRhiRenderer::pruneTileCaches() {
  for (int i = 0; i < static_cast<int>(StaticPlane::Count); ++i) {
    TilePlaneCache& cache = m_tilePlaneCaches[static_cast<size_t>(i)];
    const TileRange keep = cache.keepRange;
    if (!keep.valid) {
      for (auto& [_, tile] : cache.tiles) {
        delete tile.compositeSrb;
        delete tile.renderTarget;
        delete tile.texture;
      }
      cache.tiles.clear();
      continue;
    }

    for (auto it = cache.tiles.begin(); it != cache.tiles.end();) {
      const StaticTile& tile = it->second;
      const bool keepTile =
          (tile.tileX >= keep.minX && tile.tileX <= keep.maxX &&
           tile.tileY >= keep.minY && tile.tileY <= keep.maxY);
      if (keepTile) {
        ++it;
        continue;
      }
      delete it->second.compositeSrb;
      delete it->second.renderTarget;
      delete it->second.texture;
      it = cache.tiles.erase(it);
    }
  }
}

bool PianoRollRhiRenderer::ensureTile(StaticPlane plane, int tileX, int tileY, float dpr) {
  if (!m_rhi || !m_uniformBuffer) {
    return false;
  }

  const int idx = planeIndex(plane);
  TilePlaneCache& cache = m_tilePlaneCaches[static_cast<size_t>(idx)];
  const uint64_t key = makeTileKey(tileX, tileY);
  if (cache.tiles.find(key) != cache.tiles.end()) {
    return true;
  }

  const QSize requiredPixelSize = tilePixelSizeForDpr(dpr);
  if (requiredPixelSize != m_tilePixelSize) {
    releaseTileCaches();
    m_tilePixelSize = requiredPixelSize;
  }
  if (!m_tileSampler) {
    m_tileSampler = m_rhi->newSampler(QRhiSampler::Linear,
                                      QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge);
    m_tileSampler->create();
  }

  StaticTile tile;
  tile.tileX = tileX;
  tile.tileY = tileY;

  tile.texture = m_rhi->newTexture(QRhiTexture::RGBA8, m_tilePixelSize, 1, QRhiTexture::RenderTarget);
  if (!tile.texture->create()) {
    delete tile.texture;
    tile.texture = nullptr;
    return false;
  }

  tile.renderTarget = m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(tile.texture));
  if (!m_tileRenderPass) {
    m_tileRenderPass = tile.renderTarget->newCompatibleRenderPassDescriptor();
    if (!m_tileRenderPass) {
      delete tile.renderTarget;
      delete tile.texture;
      tile.renderTarget = nullptr;
      tile.texture = nullptr;
      return false;
    }
  }
  tile.renderTarget->setRenderPassDescriptor(m_tileRenderPass);
  if (!tile.renderTarget->create()) {
    delete tile.renderTarget;
    delete tile.texture;
    tile.renderTarget = nullptr;
    tile.texture = nullptr;
    return false;
  }

  tile.compositeSrb = m_rhi->newShaderResourceBindings();
  tile.compositeSrb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0,
                                               QRhiShaderResourceBinding::VertexStage,
                                               m_uniformBuffer),
      QRhiShaderResourceBinding::sampledTexture(1,
                                                QRhiShaderResourceBinding::FragmentStage,
                                                tile.texture,
                                                m_tileSampler),
  });
  tile.compositeSrb->create();

  tile.ready = false;
  tile.generation = 0;
  cache.tiles.emplace(key, tile);
  return true;
}

void PianoRollRhiRenderer::renderMissingTiles(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch*& initialUpdates) {
  if (!cb || !m_rhi || !m_tileUniformBuffer) {
    return;
  }
  if (!m_tileRenderPass) {
    return;
  }

  ensureTilePipeline();
  if (!m_tileRectPipeline || !m_tileRectSrb) {
    return;
  }

  auto drawPlaneInstances = [&](StaticPlane plane) {
    const auto& instances = staticPlaneInstances(plane);
    if (instances.empty()) {
      return;
    }
    QRhiBuffer* planeBuffer = staticPlaneBuffer(plane);
    if (!planeBuffer) {
      return;
    }
    const QRhiCommandBuffer::VertexInput bindings[] = {
        {m_vertexBuffer, 0},
        {planeBuffer, 0},
    };
    cb->setVertexInput(0,
                       static_cast<int>(std::size(bindings)),
                       bindings,
                       m_indexBuffer,
                       0,
                       QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, static_cast<int>(instances.size()), 0, 0, 0);
  };

  for (int i = 0; i < static_cast<int>(StaticPlane::Count); ++i) {
    const StaticPlane plane = static_cast<StaticPlane>(i);
    TilePlaneCache& cache = m_tilePlaneCaches[static_cast<size_t>(i)];

    for (uint64_t key : cache.visibleKeys) {
      auto tileIt = cache.tiles.find(key);
      if (tileIt == cache.tiles.end()) {
        continue;
      }

      StaticTile& tile = tileIt->second;
      if (tile.ready && tile.generation == m_staticGeneration) {
        continue;
      }
      if (!tile.renderTarget) {
        continue;
      }

      QMatrix4x4 mvp;
      mvp.ortho(0.0f,
                static_cast<float>(kTileLogicalWidth),
                static_cast<float>(kTileLogicalHeight),
                0.0f,
                -1.0f,
                1.0f);

      std::array<float, 20> ubo{};
      const float cameraX = static_cast<float>(tile.tileX * kTileLogicalWidth);
      const float cameraY = static_cast<float>(tile.tileY * kTileLogicalHeight);
      fillUniformData(ubo, mvp, cameraX, cameraY);

      QRhiResourceUpdateBatch* tileUpdates = initialUpdates ? initialUpdates : m_rhi->nextResourceUpdateBatch();
      tileUpdates->updateDynamicBuffer(m_tileUniformBuffer, 0, kUniformBytes, ubo.data());
      cb->beginPass(tile.renderTarget, QColor(0, 0, 0, 0), {1.0f, 0}, tileUpdates);
      cb->setViewport(QRhiViewport(0,
                                   0,
                                   static_cast<float>(m_tilePixelSize.width()),
                                   static_cast<float>(m_tilePixelSize.height())));
      cb->setGraphicsPipeline(m_tileRectPipeline);
      cb->setShaderResources(m_tileRectSrb);
      drawPlaneInstances(plane);
      cb->endPass();

      initialUpdates = nullptr;
      tile.ready = true;
      tile.generation = m_staticGeneration;
    }
  }
}

void PianoRollRhiRenderer::releaseTileCaches() {
  for (auto& cache : m_tilePlaneCaches) {
    for (auto& [_, tile] : cache.tiles) {
      delete tile.compositeSrb;
      tile.compositeSrb = nullptr;
      delete tile.renderTarget;
      tile.renderTarget = nullptr;
      delete tile.texture;
      tile.texture = nullptr;
    }
    cache.tiles.clear();
    cache.visibleKeys.clear();
    cache.keepRange = {};
  }

  delete m_tileRenderPass;
  m_tileRenderPass = nullptr;

  delete m_tileRectPipeline;
  m_tileRectPipeline = nullptr;
}

std::vector<PianoRollRhiRenderer::RectInstance>& PianoRollRhiRenderer::staticPlaneInstances(StaticPlane plane) {
  return m_staticPlaneInstanceData[static_cast<size_t>(planeIndex(plane))];
}

const std::vector<PianoRollRhiRenderer::RectInstance>& PianoRollRhiRenderer::staticPlaneInstances(StaticPlane plane) const {
  return m_staticPlaneInstanceData[static_cast<size_t>(planeIndex(plane))];
}

QRhiBuffer*& PianoRollRhiRenderer::staticPlaneBuffer(StaticPlane plane) {
  switch (plane) {
    case StaticPlane::XY:
      return m_staticXyInstanceBuffer;
    case StaticPlane::X:
      return m_staticXInstanceBuffer;
    case StaticPlane::Y:
      return m_staticYInstanceBuffer;
    case StaticPlane::Count:
      break;
  }
  return m_staticXyInstanceBuffer;
}

PianoRollRhiRenderer::Layout PianoRollRhiRenderer::computeLayout(const PianoRollFrame::Data& frame,
                                                                 const QSize& pixelSize) const {
  Layout layout;
  layout.viewWidth = pixelSize.width();
  layout.viewHeight = pixelSize.height();
  if (layout.viewWidth <= 0 || layout.viewHeight <= 0) {
    return layout;
  }

  layout.keyboardWidth = std::clamp(static_cast<float>(frame.keyboardWidth),
                                    36.0f,
                                    static_cast<float>(layout.viewWidth));
  layout.topBarHeight = std::clamp(static_cast<float>(frame.topBarHeight),
                                   14.0f,
                                   static_cast<float>(layout.viewHeight));

  layout.noteAreaLeft = layout.keyboardWidth;
  layout.noteAreaTop = layout.topBarHeight;
  layout.noteAreaWidth = std::max(0.0f, static_cast<float>(layout.viewWidth) - layout.noteAreaLeft);
  layout.noteAreaHeight = std::max(0.0f, static_cast<float>(layout.viewHeight) - layout.noteAreaTop);

  layout.pixelsPerTick = std::max(0.0001f, frame.pixelsPerTick);
  layout.pixelsPerKey = std::max(1.0f, frame.pixelsPerKey);
  layout.scrollX = static_cast<float>(std::max(0, frame.scrollX));
  layout.scrollY = static_cast<float>(std::max(0, frame.scrollY));

  const float visibleStartTickF = layout.scrollX / layout.pixelsPerTick;
  const float visibleEndTickF = (layout.scrollX + layout.noteAreaWidth) / layout.pixelsPerTick;
  layout.visibleStartTick = static_cast<uint64_t>(std::max(0.0f, std::floor(visibleStartTickF)));
  layout.visibleEndTick = static_cast<uint64_t>(std::max(0.0f, std::ceil(visibleEndTickF)));

  return layout;
}

PianoRollRhiRenderer::StaticCacheKey PianoRollRhiRenderer::makeStaticCacheKey(const PianoRollFrame::Data& frame,
                                                                               const Layout& layout,
                                                                               uint64_t trackColorsHash) const {
  StaticCacheKey key;
  key.valid = true;
  key.viewSize = QSize(layout.viewWidth, layout.viewHeight);
  key.totalTicks = frame.totalTicks;
  key.ppqn = frame.ppqn;
  key.pixelsPerTickQ = static_cast<int>(std::lround(frame.pixelsPerTick * 10000.0f));
  key.pixelsPerKeyQ = static_cast<int>(std::lround(frame.pixelsPerKey * 10000.0f));
  key.keyboardWidth = frame.keyboardWidth;
  key.topBarHeight = frame.topBarHeight;
  key.notesPtr = reinterpret_cast<uint64_t>(frame.notes.get());
  key.timeSigPtr = reinterpret_cast<uint64_t>(frame.timeSignatures.get());
  key.trackColorsHash = trackColorsHash;
  key.noteBackgroundColor = colorKey(frame.noteBackgroundColor);
  key.keyboardBackgroundColor = colorKey(frame.keyboardBackgroundColor);
  key.topBarBackgroundColor = colorKey(frame.topBarBackgroundColor);
  key.measureLineColor = colorKey(frame.measureLineColor);
  key.beatLineColor = colorKey(frame.beatLineColor);
  key.keySeparatorColor = colorKey(frame.keySeparatorColor);
  key.noteOutlineColor = colorKey(frame.noteOutlineColor);
  key.whiteKeyColor = colorKey(frame.whiteKeyColor);
  key.blackKeyColor = colorKey(frame.blackKeyColor);
  key.whiteKeyRowColor = colorKey(frame.whiteKeyRowColor);
  key.blackKeyRowColor = colorKey(frame.blackKeyRowColor);
  key.dividerColor = colorKey(frame.dividerColor);
  return key;
}

bool PianoRollRhiRenderer::staticCacheKeyEquals(const StaticCacheKey& a, const StaticCacheKey& b) const {
  return a.valid == b.valid &&
         a.viewSize == b.viewSize &&
         a.totalTicks == b.totalTicks &&
         a.ppqn == b.ppqn &&
         a.pixelsPerTickQ == b.pixelsPerTickQ &&
         a.pixelsPerKeyQ == b.pixelsPerKeyQ &&
         a.keyboardWidth == b.keyboardWidth &&
         a.topBarHeight == b.topBarHeight &&
         a.notesPtr == b.notesPtr &&
         a.timeSigPtr == b.timeSigPtr &&
         a.trackColorsHash == b.trackColorsHash &&
         a.noteBackgroundColor == b.noteBackgroundColor &&
         a.keyboardBackgroundColor == b.keyboardBackgroundColor &&
         a.topBarBackgroundColor == b.topBarBackgroundColor &&
         a.measureLineColor == b.measureLineColor &&
         a.beatLineColor == b.beatLineColor &&
         a.keySeparatorColor == b.keySeparatorColor &&
         a.noteOutlineColor == b.noteOutlineColor &&
         a.whiteKeyColor == b.whiteKeyColor &&
         a.blackKeyColor == b.blackKeyColor &&
         a.whiteKeyRowColor == b.whiteKeyRowColor &&
         a.blackKeyRowColor == b.blackKeyRowColor &&
         a.dividerColor == b.dividerColor;
}

void PianoRollRhiRenderer::buildStaticInstances(const PianoRollFrame::Data& frame,
                                                const Layout& layout,
                                                uint64_t trackColorsHash) {
  Q_UNUSED(trackColorsHash);
  m_staticFixedInstances.clear();
  for (auto& instances : m_staticPlaneInstanceData) {
    instances.clear();
  }

  auto& fixedInstances = m_staticFixedInstances;
  auto& xyInstances = staticPlaneInstances(StaticPlane::XY);
  auto& xInstances = staticPlaneInstances(StaticPlane::X);
  auto& yInstances = staticPlaneInstances(StaticPlane::Y);

  if (layout.viewWidth <= 0 || layout.viewHeight <= 0) {
    return;
  }

  appendRect(fixedInstances,
             0.0f,
             0.0f,
             layout.keyboardWidth,
             layout.topBarHeight,
             frame.keyboardBackgroundColor.darker(108));

  if (layout.noteAreaWidth <= 0.0f || layout.noteAreaHeight <= 0.0f) {
    appendRect(fixedInstances,
               layout.keyboardWidth - 1.0f,
               0.0f,
               1.0f,
               static_cast<float>(layout.viewHeight),
               frame.dividerColor);
    return;
  }

  appendRect(fixedInstances,
             layout.noteAreaLeft,
             0.0f,
             layout.noteAreaWidth,
             layout.topBarHeight,
             frame.topBarBackgroundColor);
  appendRect(fixedInstances,
             layout.noteAreaLeft,
             layout.noteAreaTop,
             layout.noteAreaWidth,
             layout.noteAreaHeight,
             frame.noteBackgroundColor);
  appendRect(fixedInstances,
             0.0f,
             layout.noteAreaTop,
             layout.keyboardWidth,
             layout.noteAreaHeight,
             frame.keyboardBackgroundColor);

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    const float keyY = layout.noteAreaTop + ((127.0f - static_cast<float>(key)) * layout.pixelsPerKey);
    const float keyH = std::max(1.0f, layout.pixelsPerKey);

    const QColor rowColor = isBlackKey(key) ? frame.blackKeyRowColor : frame.whiteKeyRowColor;
    appendRect(yInstances,
               layout.noteAreaLeft,
               keyY,
               layout.noteAreaWidth,
               keyH,
               rowColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);

    QColor rowSep = frame.keySeparatorColor;
    rowSep.setAlpha(std::max(24, rowSep.alpha()));
    appendRect(yInstances,
               layout.noteAreaLeft,
               keyY,
               layout.noteAreaWidth,
               1.0f,
               rowSep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);
  }

  std::vector<PianoRollFrame::TimeSignature> signatures;
  if (frame.timeSignatures && !frame.timeSignatures->empty()) {
    signatures = *frame.timeSignatures;
  } else {
    signatures.push_back({0, 4, 4});
  }

  std::sort(signatures.begin(), signatures.end(), [](const auto& a, const auto& b) {
    return a.tick < b.tick;
  });

  static constexpr uint64_t kMaxMeasureLines = 60000;
  static constexpr uint64_t kMaxBeatLines = 220000;
  uint64_t emittedMeasureLines = 0;
  uint64_t emittedBeatLines = 0;

  for (size_t i = 0; i < signatures.size(); ++i) {
    const auto& sig = signatures[i];
    const uint32_t numerator = std::max<uint32_t>(1, sig.numerator);
    const uint32_t denominator = std::max<uint32_t>(1, sig.denominator);
    const uint32_t beatTicks = std::max<uint32_t>(
        1,
        static_cast<uint32_t>(std::lround((static_cast<double>(frame.ppqn) * 4.0) /
                                          static_cast<double>(denominator))));
    const uint64_t measureTicks = static_cast<uint64_t>(beatTicks) * numerator;

    const uint64_t segStart = sig.tick;
    uint64_t segEnd = static_cast<uint64_t>(std::max(1, frame.totalTicks)) + 1;
    if (i + 1 < signatures.size()) {
      segEnd = std::max<uint64_t>(segStart, signatures[i + 1].tick);
    }
    if (segEnd <= segStart) {
      continue;
    }

    if (measureTicks > 0) {
      for (uint64_t tick = segStart; tick < segEnd; tick += measureTicks) {
        if (emittedMeasureLines >= kMaxMeasureLines) {
          break;
        }
        const float x = layout.noteAreaLeft + (static_cast<float>(tick) * layout.pixelsPerTick);
        appendRect(xInstances,
                   x,
                   layout.noteAreaTop,
                   1.0f,
                   layout.noteAreaHeight,
                   frame.measureLineColor,
                   LineStyle::Solid,
                   0.0f,
                   0.0f,
                   0.0f,
                   1.0f,
                   1.0f);
        QColor topMeasure = frame.measureLineColor;
        topMeasure.setAlpha(std::min(255, topMeasure.alpha() + 30));
        appendRect(xInstances,
                   x,
                   0.0f,
                   1.0f,
                   layout.topBarHeight,
                   topMeasure,
                   LineStyle::Solid,
                   0.0f,
                   0.0f,
                   0.0f,
                   1.0f,
                   1.0f);
        ++emittedMeasureLines;
      }
    }

    for (uint64_t tick = segStart; tick < segEnd; tick += beatTicks) {
      if (emittedBeatLines >= kMaxBeatLines) {
        break;
      }
      const bool isMeasure = (measureTicks > 0) && (((tick - segStart) % measureTicks) == 0);
      if (isMeasure) {
        continue;
      }

      const float x = layout.noteAreaLeft + (static_cast<float>(tick) * layout.pixelsPerTick);
      appendRect(xInstances,
                 x,
                 layout.noteAreaTop,
                 1.0f,
                 layout.noteAreaHeight,
                 frame.beatLineColor,
                 LineStyle::DottedVertical,
                 kDashSegmentPx,
                 kDashGapPx,
                 0.0f,
                 1.0f,
                 1.0f);
      QColor topBeat = frame.beatLineColor;
      topBeat.setAlpha(std::max(20, topBeat.alpha() / 2));
      appendRect(xInstances,
                 x,
                 0.0f,
                 1.0f,
                 layout.topBarHeight,
                 topBeat,
                 LineStyle::Solid,
                 0.0f,
                 0.0f,
                 0.0f,
                 1.0f,
                 1.0f);
      ++emittedBeatLines;

      if (beatTicks == 0) {
        break;
      }
    }
  }

  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(frame.trackColors.size())) {
      return frame.trackColors[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };

  if (frame.notes) {
    for (const auto& note : *frame.notes) {
      if (note.key >= PianoRollFrame::kMidiKeyCount) {
        continue;
      }
      const uint64_t noteDuration = std::max<uint64_t>(1, note.duration);
      const uint64_t noteEndTick = static_cast<uint64_t>(note.startTick) + noteDuration;
      const float x = layout.noteAreaLeft + (static_cast<float>(note.startTick) * layout.pixelsPerTick);
      const float x2 = layout.noteAreaLeft + (static_cast<float>(noteEndTick) * layout.pixelsPerTick);
      const float y = layout.noteAreaTop + ((127.0f - static_cast<float>(note.key)) * layout.pixelsPerKey);
      const float h = std::max(1.0f, layout.pixelsPerKey - 1.0f);
      const float w = std::max(0.0f, x2 - x);
      if (w <= 0.5f || h <= 0.5f) {
        continue;
      }

      QColor noteColor = colorForTrack(note.trackIndex);
      noteColor.setAlpha(188);
      appendRect(xyInstances,
                 x,
                 y,
                 w,
                 h,
                 noteColor,
                 LineStyle::Solid,
                 0.0f,
                 0.0f,
                 0.0f,
                 1.0f,
                 1.0f);

      const float edgeThickness = 1.0f;
      appendRect(xyInstances,
                 x,
                 y,
                 w,
                 edgeThickness,
                 frame.noteOutlineColor,
                 LineStyle::Solid,
                 0.0f,
                 0.0f,
                 0.0f,
                 1.0f,
                 1.0f);
      appendRect(xyInstances,
                 x,
                 y + h - edgeThickness,
                 w,
                 edgeThickness,
                 frame.noteOutlineColor,
                 LineStyle::Solid,
                 0.0f,
                 0.0f,
                 0.0f,
                 1.0f,
                 1.0f);
    }
  }

  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, layout.pixelsPerKey);
  const float blackInnerHeightRatio = 0.62f;
  const float blackInnerWidthRatio = 0.86f;
  auto centerUnitForKey = [](int key) -> float {
    return static_cast<float>(127 - key) + 0.5f;
  };

  std::array<bool, 257> seamDrawn{};
  auto drawSeamAtUnit = [&](float seamUnit) {
    const int seamId = std::clamp(static_cast<int>(std::lround(seamUnit * 2.0f)), 0, 256);
    if (seamDrawn[static_cast<size_t>(seamId)]) {
      return;
    }
    seamDrawn[static_cast<size_t>(seamId)] = true;

    const float seamY = layout.noteAreaTop + (seamUnit * layout.pixelsPerKey);

    QColor keySep = frame.keySeparatorColor;
    keySep.setAlpha(std::max(56, keySep.alpha()));
    appendRect(yInstances,
               0.0f,
               seamY,
               layout.keyboardWidth,
               1.0f,
               keySep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);
  };

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if (isBlackKey(key)) {
      continue;
    }

    int higherWhite = key + 1;
    while (higherWhite < PianoRollFrame::kMidiKeyCount && isBlackKey(higherWhite)) {
      ++higherWhite;
    }
    int lowerWhite = key - 1;
    while (lowerWhite >= 0 && isBlackKey(lowerWhite)) {
      --lowerWhite;
    }

    const float keyCenterUnit = centerUnitForKey(key);
    float topSeamUnit = keyCenterUnit - 0.5f;
    if (higherWhite < PianoRollFrame::kMidiKeyCount) {
      if (higherWhite == key + 1) {
        topSeamUnit = (keyCenterUnit + centerUnitForKey(higherWhite)) * 0.5f;
      } else {
        topSeamUnit = centerUnitForKey(key + 1);
      }
    }

    float bottomSeamUnit = keyCenterUnit + 0.5f;
    if (lowerWhite >= 0) {
      if (lowerWhite == key - 1) {
        bottomSeamUnit = (keyCenterUnit + centerUnitForKey(lowerWhite)) * 0.5f;
      } else {
        bottomSeamUnit = centerUnitForKey(key - 1);
      }
    }

    const float keyTop = layout.noteAreaTop + (topSeamUnit * layout.pixelsPerKey);
    const float keyBottom = layout.noteAreaTop + (bottomSeamUnit * layout.pixelsPerKey);
    if (keyBottom - keyTop <= 0.5f) {
      continue;
    }

    appendRect(yInstances,
               0.0f,
               keyTop,
               layout.keyboardWidth,
               keyBottom - keyTop,
               frame.whiteKeyColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);

    drawSeamAtUnit(topSeamUnit);
    drawSeamAtUnit(bottomSeamUnit);
  }

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if (!isBlackKey(key)) {
      continue;
    }

    const float centerY = layout.noteAreaTop + (centerUnitForKey(key) * layout.pixelsPerKey);
    const float blackY = centerY - (blackKeyHeight * 0.5f);
    const float clippedTop = blackY;
    const float clippedBottom = blackY + blackKeyHeight;
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    appendRect(yInstances,
               0.0f,
               clippedTop,
               blackKeyWidth,
               clippedBottom - clippedTop,
               frame.blackKeyColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);

    QColor blackFace = frame.blackKeyColor.darker(116);
    blackFace.setAlpha(230);
    const float innerW = blackKeyWidth * blackInnerWidthRatio;
    const float innerH = (clippedBottom - clippedTop) * blackInnerHeightRatio;
    const float innerX = (blackKeyWidth - innerW) * 0.5f;
    const float innerY = clippedTop + 0.6f;
    appendRect(yInstances,
               innerX,
               innerY,
               innerW,
               std::max(0.0f, innerH - 0.6f),
               blackFace,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);

    QColor blackHighlight = frame.blackKeyColor.lighter(128);
    blackHighlight.setAlpha(84);
    appendRect(yInstances,
               0.0f,
               clippedTop,
               blackKeyWidth,
               1.0f,
               blackHighlight,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               1.0f);
  }

  appendRect(fixedInstances,
             layout.noteAreaLeft,
             layout.topBarHeight - 1.0f,
             layout.noteAreaWidth,
             1.0f,
             frame.dividerColor);
  appendRect(fixedInstances,
             layout.noteAreaLeft - 1.0f,
             0.0f,
             1.0f,
             static_cast<float>(layout.viewHeight),
             frame.dividerColor);
}

void PianoRollRhiRenderer::buildDynamicInstances(const PianoRollFrame::Data& frame, const Layout& layout) {
  m_dynamicInstances.clear();

  if (layout.viewWidth <= 0 || layout.viewHeight <= 0 || layout.noteAreaWidth <= 0.0f || layout.noteAreaHeight <= 0.0f) {
    return;
  }

  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(frame.trackColors.size())) {
      return frame.trackColors[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };

  forEachVisibleNote(frame, layout, [&](const PianoRollFrame::Note& note, uint64_t noteEndTick) {
    const bool active = frame.currentTick >= static_cast<int>(note.startTick) &&
                        frame.currentTick < static_cast<int>(noteEndTick);
    if (!active) {
      return;
    }

    const NoteGeometry geometry = computeNoteGeometry(note, layout);
    if (!geometry.valid) {
      return;
    }

    QColor noteColor = colorForTrack(note.trackIndex);
    noteColor = noteColor.lighter(148);
    noteColor.setAlpha(245);
    appendRect(m_dynamicInstances, geometry.x, geometry.y, geometry.w, geometry.h, noteColor);

    const float basePhase = frame.elapsedSeconds * 2.5f + static_cast<float>(note.startTick) * 0.009f;
    for (int ring = 0; ring < 3; ++ring) {
      const float ringPhase = std::fmod(basePhase + (static_cast<float>(ring) * 0.33f), 1.0f);
      const float expand = (1.5f + (ringPhase * 13.0f) + (ring * 1.5f));
      const float intensity = (1.0f - ringPhase) * (0.62f - (ring * 0.14f));
      if (intensity <= 0.0f) {
        continue;
      }

      const float gx = std::max(layout.noteAreaLeft, geometry.x - expand);
      const float gy = std::max(layout.noteAreaTop, geometry.y - expand);
      const float gx2 = std::min(layout.noteAreaLeft + layout.noteAreaWidth, geometry.x + geometry.w + expand);
      const float gy2 = std::min(layout.noteAreaTop + layout.noteAreaHeight, geometry.y + geometry.h + expand);
      if (gx2 - gx <= 0.5f || gy2 - gy <= 0.5f) {
        continue;
      }

      QColor glow = noteColor;
      glow.setAlpha(std::clamp(static_cast<int>(std::lround(255.0f * intensity)), 0, 255));
      appendRect(m_dynamicInstances, gx, gy, gx2 - gx, gy2 - gy, glow);
    }

    QColor noteEdge = frame.noteOutlineColor;
    noteEdge.setAlpha(std::min(255, noteEdge.alpha() + 95));
    const float edgeThickness = 1.0f;
    appendRect(m_dynamicInstances,
               geometry.x,
               geometry.y,
               geometry.w,
               edgeThickness,
               noteEdge);
    appendRect(m_dynamicInstances,
               geometry.x,
               geometry.y + geometry.h - edgeThickness,
               geometry.w,
               edgeThickness,
               noteEdge);
  });

  const float currentX = layout.noteAreaLeft + (static_cast<float>(frame.currentTick) * layout.pixelsPerTick) - layout.scrollX;
  if (currentX >= layout.noteAreaLeft - 2.0f && currentX <= layout.noteAreaLeft + layout.noteAreaWidth + 2.0f) {
    QColor scanColor = frame.scanLineColor;
    if (!frame.playbackActive) {
      scanColor.setAlpha(std::max(100, scanColor.alpha() / 2));
    }

    appendRect(m_dynamicInstances, currentX - 1.0f, layout.noteAreaTop, 2.0f, layout.noteAreaHeight, scanColor);
    appendRect(m_dynamicInstances, currentX - 1.0f, 0.0f, 2.0f, layout.topBarHeight, scanColor);
    appendRect(m_dynamicInstances, currentX - 4.0f, 0.0f, 8.0f, 3.0f, scanColor);

    QColor progress = frame.topBarProgressColor;
    progress.setAlpha(std::min(255, progress.alpha() + 45));
    const float progressWidth = std::clamp(currentX - layout.noteAreaLeft, 0.0f, layout.noteAreaWidth);
    if (progressWidth > 0.5f) {
      appendRect(m_dynamicInstances, layout.noteAreaLeft, 0.0f, progressWidth, layout.topBarHeight, progress);
    }
  }

  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, layout.pixelsPerKey);
  auto centerUnitForKey = [](int key) -> float {
    return static_cast<float>(127 - key) + 0.5f;
  };

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if (isBlackKey(key)) {
      continue;
    }

    int higherWhite = key + 1;
    while (higherWhite < PianoRollFrame::kMidiKeyCount && isBlackKey(higherWhite)) {
      ++higherWhite;
    }
    int lowerWhite = key - 1;
    while (lowerWhite >= 0 && isBlackKey(lowerWhite)) {
      --lowerWhite;
    }

    const float keyCenterUnit = centerUnitForKey(key);
    float topSeamUnit = keyCenterUnit - 0.5f;
    if (higherWhite < PianoRollFrame::kMidiKeyCount) {
      if (higherWhite == key + 1) {
        topSeamUnit = (keyCenterUnit + centerUnitForKey(higherWhite)) * 0.5f;
      } else {
        topSeamUnit = centerUnitForKey(key + 1);
      }
    }

    float bottomSeamUnit = keyCenterUnit + 0.5f;
    if (lowerWhite >= 0) {
      if (lowerWhite == key - 1) {
        bottomSeamUnit = (keyCenterUnit + centerUnitForKey(lowerWhite)) * 0.5f;
      } else {
        bottomSeamUnit = centerUnitForKey(key - 1);
      }
    }

    const float keyTop = layout.noteAreaTop + (topSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float keyBottom = layout.noteAreaTop + (bottomSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float clippedTop = std::max(layout.noteAreaTop, keyTop);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, keyBottom);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0) {
      continue;
    }

    QColor active = colorForTrack(activeTrack).lighter(112);
    active.setAlpha(172);
    appendRect(m_dynamicInstances,
               1.0f,
               clippedTop + 1.0f,
               std::max(0.0f, layout.keyboardWidth - 2.0f),
               std::max(0.0f, clippedBottom - clippedTop - 2.0f),
               active);
  }

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if (!isBlackKey(key)) {
      continue;
    }

    const float centerY = layout.noteAreaTop + (centerUnitForKey(key) * layout.pixelsPerKey) - layout.scrollY;
    const float blackY = centerY - (blackKeyHeight * 0.5f);
    const float clippedTop = std::max(layout.noteAreaTop, blackY);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, blackY + blackKeyHeight);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0) {
      continue;
    }

    QColor active = colorForTrack(activeTrack).lighter(133);
    active.setAlpha(238);
    appendRect(m_dynamicInstances,
               1.0f,
               clippedTop + 1.0f,
               std::max(0.0f, blackKeyWidth - 2.0f),
               std::max(0.0f, clippedBottom - clippedTop - 2.0f),
               active);
  }
}

template <typename Fn>
void PianoRollRhiRenderer::forEachVisibleNote(const PianoRollFrame::Data& frame,
                                              const Layout& layout,
                                              Fn&& fn) const {
  if (!frame.notes || frame.notes->empty()) {
    return;
  }

  const auto& notes = *frame.notes;
  const uint64_t maxDuration = std::max<uint64_t>(1, frame.maxNoteDurationTicks);
  const uint64_t searchStart = (layout.visibleStartTick > maxDuration)
                                   ? (layout.visibleStartTick - maxDuration)
                                   : 0;

  auto it = std::lower_bound(notes.begin(),
                             notes.end(),
                             searchStart,
                             [](const PianoRollFrame::Note& note, uint64_t tick) {
                               return note.startTick < tick;
                             });

  for (; it != notes.end(); ++it) {
    if (it->startTick > layout.visibleEndTick) {
      break;
    }

    const uint64_t noteDuration = std::max<uint64_t>(1, it->duration);
    const uint64_t noteEndTick = static_cast<uint64_t>(it->startTick) + noteDuration;
    if (noteEndTick < layout.visibleStartTick) {
      continue;
    }

    fn(*it, noteEndTick);
  }
}

PianoRollRhiRenderer::NoteGeometry PianoRollRhiRenderer::computeNoteGeometry(const PianoRollFrame::Note& note,
                                                                             const Layout& layout) const {
  NoteGeometry geometry;
  if (note.key >= PianoRollFrame::kMidiKeyCount) {
    return geometry;
  }

  const uint64_t noteDuration = std::max<uint64_t>(1, note.duration);
  const uint64_t noteEndTick = static_cast<uint64_t>(note.startTick) + noteDuration;

  const float x = layout.noteAreaLeft + (static_cast<float>(note.startTick) * layout.pixelsPerTick) - layout.scrollX;
  const float x2 = layout.noteAreaLeft + (static_cast<float>(noteEndTick) * layout.pixelsPerTick) - layout.scrollX;
  const float y = layout.noteAreaTop + ((127.0f - static_cast<float>(note.key)) * layout.pixelsPerKey) - layout.scrollY;
  const float h = std::max(1.0f, layout.pixelsPerKey - 1.0f);

  const float clippedX = std::max(layout.noteAreaLeft, x);
  const float clippedX2 = std::min(layout.noteAreaLeft + layout.noteAreaWidth, x2);
  const float clippedY = std::max(layout.noteAreaTop, y);
  const float clippedY2 = std::min(layout.noteAreaTop + layout.noteAreaHeight, y + h);

  geometry.x = clippedX;
  geometry.y = clippedY;
  geometry.w = clippedX2 - clippedX;
  geometry.h = clippedY2 - clippedY;
  geometry.valid = (geometry.w > 0.5f && geometry.h > 0.5f);
  return geometry;
}

void PianoRollRhiRenderer::appendRect(std::vector<RectInstance>& instances,
                                      float x,
                                      float y,
                                      float w,
                                      float h,
                                      const QColor& color,
                                      LineStyle style,
                                      float segment,
                                      float gap,
                                      float phase,
                                      float scrollMulX,
                                      float scrollMulY) {
  if (w <= 0.0f || h <= 0.0f || color.alpha() <= 0) {
    return;
  }

  instances.push_back(RectInstance{
      x,
      y,
      w,
      h,
      color.redF(),
      color.greenF(),
      color.blueF(),
      color.alphaF(),
      static_cast<float>(static_cast<int>(style)),
      segment,
      gap,
      phase,
      scrollMulX,
      scrollMulY,
  });
}
