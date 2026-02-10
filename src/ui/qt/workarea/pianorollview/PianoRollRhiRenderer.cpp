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
#include <limits>
#include <utility>

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

  m_staticBuffersUploaded = false;
  m_inited = true;
}

void PianoRollRhiRenderer::releaseResources() {
  if (m_rhi) {
    m_rhi->makeThreadLocalNativeContextCurrent();
  }

  delete m_pipeline;
  m_pipeline = nullptr;

  delete m_shaderBindings;
  m_shaderBindings = nullptr;

  delete m_staticBackInstanceBuffer;
  m_staticBackInstanceBuffer = nullptr;

  delete m_staticFrontInstanceBuffer;
  m_staticFrontInstanceBuffer = nullptr;

  delete m_dynamicInstanceBuffer;
  m_dynamicInstanceBuffer = nullptr;

  delete m_uniformBuffer;
  m_uniformBuffer = nullptr;

  delete m_indexBuffer;
  m_indexBuffer = nullptr;

  delete m_vertexBuffer;
  m_vertexBuffer = nullptr;

  m_outputRenderPass = nullptr;
  m_sampleCount = 1;
  m_staticBuffersUploaded = false;
  m_inited = false;
  m_staticBackInstances.clear();
  m_staticFrontInstances.clear();
  m_dynamicInstances.clear();
  m_dynamicFrontStart = 0;
  clearStaticBucketCache();
  m_staticBucketStyleHash = 0;
  m_activeStaticBucketId = 0;
  m_frameSerial = 0;
  m_hasActiveStaticBucket = false;
  m_staticDataDirty = true;
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

  // Work in logical coordinates; shaders/projector handle device pixels via viewport.
  QSize logicalSize = frame.viewportSize;
  if (logicalSize.isEmpty()) {
    const float dpr = std::max(1.0f, target.dpr);
    logicalSize = QSize(std::max(1, static_cast<int>(std::lround(static_cast<float>(target.pixelSize.width()) / dpr))),
                        std::max(1, static_cast<int>(std::lround(static_cast<float>(target.pixelSize.height()) / dpr))));
  }

  const Layout layout = computeLayout(frame, logicalSize);
  const uint64_t trackColorsHash = hashTrackColors(frame.trackColors);
  const StaticCacheKey key = makeStaticCacheKey(frame, layout, trackColorsHash);
  const uint64_t styleHash = staticBucketStyleHash(key);
  const uint64_t bucketId = staticBucketId(key);

  if (m_staticBucketStyleHash != styleHash) {
    clearStaticBucketCache();
    m_staticBucketStyleHash = styleHash;
  }

  auto bucketIt = m_staticBucketCache.find(bucketId);
  if (bucketIt == m_staticBucketCache.end()) {
    buildStaticInstances(frame, layout, trackColorsHash, key);

    CachedStaticBucket bucket;
    bucket.backInstances = m_staticBackInstances;
    bucket.frontInstances = m_staticFrontInstances;
    bucket.lastUsedFrame = ++m_frameSerial;
    bucketIt = m_staticBucketCache.emplace(bucketId, std::move(bucket)).first;
    trimStaticBucketCache();
  } else {
    bucketIt->second.lastUsedFrame = ++m_frameSerial;
  }

  if (!m_hasActiveStaticBucket || m_activeStaticBucketId != bucketId) {
    m_staticBackInstances = bucketIt->second.backInstances;
    m_staticFrontInstances = bucketIt->second.frontInstances;
    m_activeStaticBucketId = bucketId;
    m_hasActiveStaticBucket = true;
    m_staticDataDirty = true;
  }
  // Dynamic overlays (active notes, scanline, progress) update every frame.
  buildDynamicInstances(frame, layout);

  ensurePipeline(target.renderPassDesc, std::max(1, target.sampleCount));

  QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
  if (!m_staticBuffersUploaded) {
    updates->uploadStaticBuffer(m_vertexBuffer, kVertices);
    updates->uploadStaticBuffer(m_indexBuffer, kIndices);
    m_staticBuffersUploaded = true;
  }

  QMatrix4x4 mvp;
  mvp.ortho(0.0f,
            static_cast<float>(logicalSize.width()),
            static_cast<float>(logicalSize.height()),
            0.0f,
            -1.0f,
            1.0f);
  std::array<float, 20> ubo{};
  std::memcpy(ubo.data(), mvp.constData(), kMat4Bytes);
  // Scroll values are consumed by shader fields that opt into world scrolling.
  ubo[16] = layout.scrollX;
  ubo[17] = layout.scrollY;
  ubo[18] = 0.0f;
  ubo[19] = 0.0f;
  updates->updateDynamicBuffer(m_uniformBuffer, 0, kUniformBytes, ubo.data());

  if (m_staticDataDirty) {
    if (!m_staticBackInstances.empty()) {
      const int staticBackBytes = static_cast<int>(m_staticBackInstances.size() * sizeof(RectInstance));
      if (ensureInstanceBuffer(m_staticBackInstanceBuffer, staticBackBytes, 16384)) {
        updates->updateDynamicBuffer(m_staticBackInstanceBuffer, 0, staticBackBytes, m_staticBackInstances.data());
      }
    }

    if (!m_staticFrontInstances.empty()) {
      const int staticFrontBytes = static_cast<int>(m_staticFrontInstances.size() * sizeof(RectInstance));
      if (ensureInstanceBuffer(m_staticFrontInstanceBuffer, staticFrontBytes, 4096)) {
        updates->updateDynamicBuffer(m_staticFrontInstanceBuffer, 0, staticFrontBytes, m_staticFrontInstances.data());
      }
    }
    m_staticDataDirty = false;
  }

  if (!m_dynamicInstances.empty()) {
    const int dynamicBytes = static_cast<int>(m_dynamicInstances.size() * sizeof(RectInstance));
    if (ensureInstanceBuffer(m_dynamicInstanceBuffer, dynamicBytes, 8192)) {
      updates->updateDynamicBuffer(m_dynamicInstanceBuffer, 0, dynamicBytes, m_dynamicInstances.data());
    }
  }

  cb->beginPass(target.renderTarget, frame.backgroundColor, {1.0f, 0}, updates);
  cb->setViewport(QRhiViewport(0,
                               0,
                               static_cast<float>(target.pixelSize.width()),
                               static_cast<float>(target.pixelSize.height())));

  if (m_pipeline && m_shaderBindings) {
    cb->setGraphicsPipeline(m_pipeline);
    cb->setShaderResources(m_shaderBindings);

    auto drawInstances = [&](QRhiBuffer* buffer, int count, int firstInstance = 0) {
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
      cb->drawIndexed(6, count, 0, 0, firstInstance);
    };

    const int dynamicCount = static_cast<int>(m_dynamicInstances.size());
    const int dynamicFrontStart = std::clamp(m_dynamicFrontStart, 0, dynamicCount);
    drawInstances(m_staticBackInstanceBuffer, static_cast<int>(m_staticBackInstances.size()));
    drawInstances(m_dynamicInstanceBuffer, dynamicFrontStart, 0);
    drawInstances(m_staticFrontInstanceBuffer, static_cast<int>(m_staticFrontInstances.size()));
    drawInstances(m_dynamicInstanceBuffer, dynamicCount - dynamicFrontStart, dynamicFrontStart);
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

void PianoRollRhiRenderer::ensurePipeline(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount) {
  if (!renderPassDesc) {
    return;
  }

  if (m_pipeline && m_shaderBindings && m_outputRenderPass == renderPassDesc && m_sampleCount == sampleCount) {
    return;
  }

  // Render pass/sample count changes require a fresh pipeline.
  delete m_pipeline;
  m_pipeline = nullptr;

  delete m_shaderBindings;
  m_shaderBindings = nullptr;

  m_outputRenderPass = renderPassDesc;
  m_sampleCount = sampleCount;

  m_shaderBindings = m_rhi->newShaderResourceBindings();
  m_shaderBindings->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0,
                                               QRhiShaderResourceBinding::VertexStage,
                                               m_uniformBuffer),
  });
  m_shaderBindings->create();

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

  m_pipeline = m_rhi->newGraphicsPipeline();
  m_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
  m_pipeline->setSampleCount(m_sampleCount);
  m_pipeline->setShaderStages({
      {QRhiShaderStage::Vertex, vertexShader},
      {QRhiShaderStage::Fragment, fragmentShader},
  });
  m_pipeline->setShaderResourceBindings(m_shaderBindings);
  m_pipeline->setVertexInputLayout(inputLayout);

  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::One;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  m_pipeline->setTargetBlends({blend});

  m_pipeline->setRenderPassDescriptor(renderPassDesc);
  m_pipeline->create();
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
  {
    const uint64_t totalTickEnd = static_cast<uint64_t>(std::max(1, frame.totalTicks)) + 1;
    const uint64_t viewportTicks =
        std::max<uint64_t>(1, static_cast<uint64_t>(std::ceil(layout.noteAreaWidth / layout.pixelsPerTick)));
    const uint64_t bucketTicks = std::max<uint64_t>(64, viewportTicks / 2);
    const uint64_t bucketStart = (layout.visibleStartTick / bucketTicks) * bucketTicks;
    const uint64_t prePad = viewportTicks;
    const uint64_t postPad = viewportTicks * 2;
    key.staticTickStart = (bucketStart > prePad) ? (bucketStart - prePad) : 0;
    key.staticTickEnd = std::min<uint64_t>(totalTickEnd, bucketStart + bucketTicks + postPad);
  }
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

uint64_t PianoRollRhiRenderer::staticBucketStyleHash(const StaticCacheKey& key) const {
  uint64_t hash = 1469598103934665603ULL;
  auto hashMix = [&hash](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };

  hashMix(static_cast<uint64_t>(key.viewSize.width()));
  hashMix(static_cast<uint64_t>(key.viewSize.height()));
  hashMix(static_cast<uint64_t>(key.totalTicks));
  hashMix(static_cast<uint64_t>(key.ppqn));
  hashMix(static_cast<uint64_t>(key.pixelsPerTickQ));
  hashMix(static_cast<uint64_t>(key.pixelsPerKeyQ));
  hashMix(static_cast<uint64_t>(key.keyboardWidth));
  hashMix(static_cast<uint64_t>(key.topBarHeight));
  hashMix(key.notesPtr);
  hashMix(key.timeSigPtr);
  hashMix(key.trackColorsHash);
  hashMix(key.noteBackgroundColor);
  hashMix(key.keyboardBackgroundColor);
  hashMix(key.topBarBackgroundColor);
  hashMix(key.measureLineColor);
  hashMix(key.beatLineColor);
  hashMix(key.keySeparatorColor);
  hashMix(key.noteOutlineColor);
  hashMix(key.whiteKeyColor);
  hashMix(key.blackKeyColor);
  hashMix(key.whiteKeyRowColor);
  hashMix(key.blackKeyRowColor);
  hashMix(key.dividerColor);
  return hash;
}

uint64_t PianoRollRhiRenderer::staticBucketId(const StaticCacheKey& key) const {
  uint64_t hash = 1469598103934665603ULL;
  hash ^= key.staticTickStart;
  hash *= 1099511628211ULL;
  hash ^= key.staticTickEnd;
  hash *= 1099511628211ULL;
  return hash;
}

void PianoRollRhiRenderer::trimStaticBucketCache() {
  static constexpr size_t kMaxCachedBuckets = 10;
  while (m_staticBucketCache.size() > kMaxCachedBuckets) {
    auto lruIt = m_staticBucketCache.end();
    for (auto it = m_staticBucketCache.begin(); it != m_staticBucketCache.end(); ++it) {
      if (m_hasActiveStaticBucket && it->first == m_activeStaticBucketId) {
        continue;
      }
      if (lruIt == m_staticBucketCache.end() || it->second.lastUsedFrame < lruIt->second.lastUsedFrame) {
        lruIt = it;
      }
    }
    if (lruIt == m_staticBucketCache.end()) {
      break;
    }
    m_staticBucketCache.erase(lruIt);
  }
}

void PianoRollRhiRenderer::clearStaticBucketCache() {
  m_staticBucketCache.clear();
  m_hasActiveStaticBucket = false;
  m_activeStaticBucketId = 0;
  m_staticDataDirty = true;
}

void PianoRollRhiRenderer::buildStaticInstances(const PianoRollFrame::Data& frame,
                                                const Layout& layout,
                                                uint64_t trackColorsHash,
                                                const StaticCacheKey& cacheKey) {
  Q_UNUSED(trackColorsHash);
  m_staticBackInstances.clear();
  m_staticFrontInstances.clear();

  if (layout.viewWidth <= 0 || layout.viewHeight <= 0) {
    return;
  }

  appendRect(m_staticBackInstances,
             0.0f,
             0.0f,
             layout.keyboardWidth,
             layout.topBarHeight,
             frame.keyboardBackgroundColor.darker(108));

  if (layout.noteAreaWidth <= 0.0f || layout.noteAreaHeight <= 0.0f) {
    appendRect(m_staticBackInstances,
               layout.keyboardWidth - 1.0f,
               0.0f,
               1.0f,
               static_cast<float>(layout.viewHeight),
               frame.dividerColor);
    return;
  }

  appendRect(m_staticBackInstances,
             layout.noteAreaLeft,
             0.0f,
             layout.noteAreaWidth,
             layout.topBarHeight,
             frame.topBarBackgroundColor);
  appendRect(m_staticBackInstances,
             layout.noteAreaLeft,
             layout.noteAreaTop,
             layout.noteAreaWidth,
             layout.noteAreaHeight,
             frame.noteBackgroundColor);
  appendRect(m_staticBackInstances,
             0.0f,
             layout.noteAreaTop,
             layout.keyboardWidth,
             layout.noteAreaHeight,
             frame.keyboardBackgroundColor);

  const uint64_t staticTickStart = cacheKey.staticTickStart;
  const uint64_t staticTickEnd = std::max<uint64_t>(staticTickStart + 1, cacheKey.staticTickEnd);

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    const float keyY = layout.noteAreaTop + ((127.0f - static_cast<float>(key)) * layout.pixelsPerKey);
    const float keyH = std::max(1.0f, layout.pixelsPerKey);

    const QColor rowColor = isBlackKey(key) ? frame.blackKeyRowColor : frame.whiteKeyRowColor;
    appendRect(m_staticBackInstances,
               layout.noteAreaLeft,
               keyY,
               layout.noteAreaWidth,
               keyH,
               rowColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f);

    QColor rowSep = frame.keySeparatorColor;
    rowSep.setAlpha(std::max(24, rowSep.alpha()));
    appendRect(m_staticBackInstances,
               layout.noteAreaLeft,
               keyY,
               layout.noteAreaWidth,
               1.0f,
               rowSep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
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

  // Hard caps keep pathological signatures/lengths from exploding CPU time.
  static constexpr uint64_t kMaxMeasureLines = 60000;
  static constexpr uint64_t kMaxBeatLines = 220000;
  static constexpr float kMinMeasureLineSpacingPx = 24.0f;
  static constexpr float kMinBeatLineSpacingPx = 8.0f;
  uint64_t emittedMeasureLines = 0;
  uint64_t emittedBeatLines = 0;
  auto alignTick = [](uint64_t value, uint64_t origin, uint64_t step) -> uint64_t {
    if (step == 0 || value <= origin) {
      return origin;
    }
    const uint64_t delta = value - origin;
    const uint64_t remainder = delta % step;
    return (remainder == 0) ? value : (value + (step - remainder));
  };
  auto mulSaturated = [](uint64_t a, uint64_t b) -> uint64_t {
    if (a == 0 || b == 0) {
      return 0;
    }
    if (a > (std::numeric_limits<uint64_t>::max() / b)) {
      return std::numeric_limits<uint64_t>::max();
    }
    return a * b;
  };

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
    const uint64_t drawStart = std::max<uint64_t>(segStart, staticTickStart);
    const uint64_t drawEnd = std::min<uint64_t>(segEnd, staticTickEnd);
    if (drawEnd <= drawStart) {
      continue;
    }

    if (measureTicks > 0) {
      const float measureSpacingPx = static_cast<float>(measureTicks) * layout.pixelsPerTick;
      const uint64_t measureStride = (measureSpacingPx >= kMinMeasureLineSpacingPx)
                                         ? 1
                                         : std::max<uint64_t>(
                                               1,
                                               static_cast<uint64_t>(
                                                   std::ceil(kMinMeasureLineSpacingPx /
                                                             std::max(0.0001f, measureSpacingPx))));
      const uint64_t measureStep = mulSaturated(measureTicks, measureStride);
      if (measureStep > 0) {
        for (uint64_t tick = alignTick(drawStart, segStart, measureStep); tick < drawEnd;) {
          if (emittedMeasureLines >= kMaxMeasureLines) {
            break;
          }
          const float x = layout.noteAreaLeft + (static_cast<float>(tick) * layout.pixelsPerTick);
          appendRect(m_staticBackInstances,
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
                     0.0f);
          QColor topMeasure = frame.measureLineColor;
          topMeasure.setAlpha(std::min(255, topMeasure.alpha() + 30));
          appendRect(m_staticBackInstances,
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
                     0.0f);
          ++emittedMeasureLines;

          const uint64_t nextTick = tick + measureStep;
          if (nextTick <= tick) {
            break;
          }
          tick = nextTick;
        }
      }
    }

    if (beatTicks > 0) {
      const float beatSpacingPx = static_cast<float>(beatTicks) * layout.pixelsPerTick;
      const uint64_t beatStride = (beatSpacingPx >= kMinBeatLineSpacingPx)
                                      ? 1
                                      : std::max<uint64_t>(
                                            1,
                                            static_cast<uint64_t>(
                                                std::ceil(kMinBeatLineSpacingPx /
                                                          std::max(0.0001f, beatSpacingPx))));
      const uint64_t beatStep = mulSaturated(beatTicks, beatStride);
      if (beatStep > 0) {
        for (uint64_t tick = alignTick(drawStart, segStart, beatStep); tick < drawEnd;) {
          if (emittedBeatLines >= kMaxBeatLines) {
            break;
          }
          const bool isMeasure = (measureTicks > 0) && (((tick - segStart) % measureTicks) == 0);
          if (!isMeasure) {
            const float x = layout.noteAreaLeft + (static_cast<float>(tick) * layout.pixelsPerTick);
            appendRect(m_staticBackInstances,
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
                       0.0f);
            QColor topBeat = frame.beatLineColor;
            topBeat.setAlpha(std::max(20, topBeat.alpha() / 2));
            appendRect(m_staticBackInstances,
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
                       0.0f);
            ++emittedBeatLines;
          }

          const uint64_t nextTick = tick + beatStep;
          if (nextTick <= tick) {
            break;
          }
          tick = nextTick;
        }
      }
    }
  }

  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(frame.trackColors.size())) {
      return frame.trackColors[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };

  forEachNoteInRange(frame, staticTickStart, staticTickEnd, [&](const PianoRollFrame::Note& note, uint64_t noteEndTick) {
    if (note.key >= PianoRollFrame::kMidiKeyCount) {
      return;
    }

    const float x = layout.noteAreaLeft + (static_cast<float>(note.startTick) * layout.pixelsPerTick);
    const float x2 = layout.noteAreaLeft + (static_cast<float>(noteEndTick) * layout.pixelsPerTick);
    const float y = layout.noteAreaTop + ((127.0f - static_cast<float>(note.key)) * layout.pixelsPerKey);
    const float h = std::max(1.0f, layout.pixelsPerKey - 1.0f);
    const float w = std::max(0.0f, x2 - x);
    if (w <= 0.5f || h <= 0.5f) {
      return;
    }

    QColor noteColor = colorForTrack(note.trackIndex);
    noteColor.setAlpha(188);
    appendRect(m_staticBackInstances,
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
    appendRect(m_staticBackInstances,
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
    appendRect(m_staticBackInstances,
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
  });

  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, layout.pixelsPerKey);
  const float blackInnerHeightRatio = 0.62f;
  const float blackInnerWidthRatio = 0.86f;
  // Keys are aligned to 12 equal note lanes, then white-key seams are derived
  // from neighboring white notes to mimic a real keyboard silhouette.
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
    appendRect(m_staticBackInstances,
               0.0f,
               seamY,
               layout.keyboardWidth,
               1.0f,
               keySep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
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

    appendRect(m_staticBackInstances,
               0.0f,
               keyTop,
               layout.keyboardWidth,
               keyBottom - keyTop,
               frame.whiteKeyColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
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

    appendRect(m_staticFrontInstances,
               0.0f,
               clippedTop,
               blackKeyWidth,
               clippedBottom - clippedTop,
               frame.blackKeyColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f);

    QColor blackFace = frame.blackKeyColor.darker(116);
    blackFace.setAlpha(230);
    const float innerW = blackKeyWidth * blackInnerWidthRatio;
    const float innerH = (clippedBottom - clippedTop) * blackInnerHeightRatio;
    const float innerX = (blackKeyWidth - innerW) * 0.5f;
    const float innerY = clippedTop + 0.6f;
    appendRect(m_staticFrontInstances,
               innerX,
               innerY,
               innerW,
               std::max(0.0f, innerH - 0.6f),
               blackFace,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f);

    QColor blackHighlight = frame.blackKeyColor.lighter(128);
    blackHighlight.setAlpha(84);
    appendRect(m_staticFrontInstances,
               0.0f,
               clippedTop,
               blackKeyWidth,
               1.0f,
               blackHighlight,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f);
  }

  appendRect(m_staticBackInstances,
             layout.noteAreaLeft,
             layout.topBarHeight - 1.0f,
             layout.noteAreaWidth,
             1.0f,
             frame.dividerColor);
  appendRect(m_staticBackInstances,
             layout.noteAreaLeft - 1.0f,
             0.0f,
             1.0f,
             static_cast<float>(layout.viewHeight),
             frame.dividerColor);
}

void PianoRollRhiRenderer::buildDynamicInstances(const PianoRollFrame::Data& frame, const Layout& layout) {
  m_dynamicInstances.clear();
  m_dynamicFrontStart = 0;

  if (layout.viewWidth <= 0 || layout.viewHeight <= 0 || layout.noteAreaWidth <= 0.0f || layout.noteAreaHeight <= 0.0f) {
    return;
  }

  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(frame.trackColors.size())) {
      return frame.trackColors[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };

  if (frame.activeNotes && !frame.activeNotes->empty()) {
    for (const PianoRollFrame::Note& note : *frame.activeNotes) {
      const NoteGeometry geometry = computeNoteGeometry(note, layout);
      if (!geometry.valid) {
        continue;
      }

      QColor noteColor = colorForTrack(note.trackIndex);
      noteColor = noteColor.lighter(148);
      noteColor.setAlpha(245);
      appendRect(m_dynamicInstances, geometry.x, geometry.y, geometry.w, geometry.h, noteColor);

      const float basePhase = frame.elapsedSeconds * 2.5f + static_cast<float>(note.startTick) * 0.009f;
      // Layered expanding rings approximate a subtle pulse/glow without extra passes.
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
    }
  }

  if (frame.selectedNotes && !frame.selectedNotes->empty()) {
    const float edgeThickness = 1.0f;
    for (const PianoRollFrame::Note& selected : *frame.selectedNotes) {
      const NoteGeometry geometry = computeNoteGeometry(selected, layout);
      if (!geometry.valid) {
        continue;
      }

      appendRect(m_dynamicInstances,
                 geometry.x,
                 geometry.y,
                 geometry.w,
                 geometry.h,
                 frame.selectedNoteFillColor);

      appendRect(m_dynamicInstances,
                 geometry.x,
                 geometry.y,
                 geometry.w,
                 edgeThickness,
                 frame.selectedNoteOutlineColor);
      appendRect(m_dynamicInstances,
                 geometry.x,
                 geometry.y + geometry.h - edgeThickness,
                 geometry.w,
                 edgeThickness,
                 frame.selectedNoteOutlineColor);
      appendRect(m_dynamicInstances,
                 geometry.x,
                 geometry.y,
                 edgeThickness,
                 geometry.h,
                 frame.selectedNoteOutlineColor);
      appendRect(m_dynamicInstances,
                 geometry.x + geometry.w - edgeThickness,
                 geometry.y,
                 edgeThickness,
                 geometry.h,
                 frame.selectedNoteOutlineColor);
    }
  }

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

  if (frame.selectionRectVisible && frame.selectionRectW > 0.5f && frame.selectionRectH > 0.5f) {
    appendRect(m_dynamicInstances,
               frame.selectionRectX,
               frame.selectionRectY,
               frame.selectionRectW,
               frame.selectionRectH,
               frame.selectionRectFillColor);

    const float edge = 1.0f;
    appendRect(m_dynamicInstances,
               frame.selectionRectX,
               frame.selectionRectY,
               frame.selectionRectW,
               edge,
               frame.selectionRectOutlineColor);
    appendRect(m_dynamicInstances,
               frame.selectionRectX,
               frame.selectionRectY + frame.selectionRectH - edge,
               frame.selectionRectW,
               edge,
               frame.selectionRectOutlineColor);
    appendRect(m_dynamicInstances,
               frame.selectionRectX,
               frame.selectionRectY,
               edge,
               frame.selectionRectH,
               frame.selectionRectOutlineColor);
    appendRect(m_dynamicInstances,
               frame.selectionRectX + frame.selectionRectW - edge,
               frame.selectionRectY,
               edge,
               frame.selectionRectH,
               frame.selectionRectOutlineColor);
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
               0.0f,
               clippedTop + 1.0f,
               std::max(0.0f, layout.keyboardWidth),
               std::max(0.0f, clippedBottom - clippedTop - 1.0f),
               active);
  }

  // Black-key highlights must render above the static black key faces.
  m_dynamicFrontStart = static_cast<int>(m_dynamicInstances.size());
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
void PianoRollRhiRenderer::forEachNoteInRange(const PianoRollFrame::Data& frame,
                                              uint64_t startTick,
                                              uint64_t endTick,
                                              Fn&& fn) const {
  if (!frame.notes || frame.notes->empty()) {
    return;
  }

  const auto& notes = *frame.notes;
  const uint64_t maxDuration = std::max<uint64_t>(1, frame.maxNoteDurationTicks);
  const uint64_t searchStart = (startTick > maxDuration)
                                   ? (startTick - maxDuration)
                                   : 0;

  auto it = std::lower_bound(notes.begin(),
                             notes.end(),
                             searchStart,
                             [](const PianoRollFrame::Note& note, uint64_t tick) {
                               return note.startTick < tick;
                             });

  for (; it != notes.end(); ++it) {
    if (it->startTick > endTick) {
      break;
    }

    const uint64_t noteDuration = std::max<uint64_t>(1, it->duration);
    const uint64_t noteEndTick = static_cast<uint64_t>(it->startTick) + noteDuration;
    if (noteEndTick < startTick) {
      continue;
    }

    fn(*it, noteEndTick);
  }
}

template <typename Fn>
void PianoRollRhiRenderer::forEachVisibleNote(const PianoRollFrame::Data& frame,
                                              const Layout& layout,
                                              Fn&& fn) const {
  // Start search a bit before the viewport so long notes crossing into view are included.
  forEachNoteInRange(frame, layout.visibleStartTick, layout.visibleEndTick, std::forward<Fn>(fn));
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
