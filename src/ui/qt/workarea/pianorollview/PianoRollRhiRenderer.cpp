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
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QString>

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
static constexpr int kUniformBytes = (16 + 4 + 4 + 4) * sizeof(float);
static constexpr int kMeasureLabelFontPixelSize = 11;
static constexpr int kMeasureLabelPaddingPx = 2;

bool isBlackMidiKey(int key) {
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

struct KeyboardKeyTopology {
  float centerUnit = 0.0f;
  float topSeamUnit = 0.0f;
  float bottomSeamUnit = 0.0f;
};

struct KeyboardTopology {
  std::array<KeyboardKeyTopology, PianoRollFrame::kMidiKeyCount> keys;
  std::vector<int> whiteKeys;
  std::vector<int> blackKeys;
  std::vector<float> seamUnits;
};

// Enables optional profiling with `VGMTRANS_PROFILE_PIANOROLL=1`.
bool pianoRollProfileEnabled() {
  static const bool enabled = qEnvironmentVariableIntValue("VGMTRANS_PROFILE_PIANOROLL") > 0;
  return enabled;
}

struct RenderProfileSample {
  qint64 captureNs = 0;
  qint64 staticNs = 0;
  qint64 dynamicNs = 0;
  qint64 uploadNs = 0;
  qint64 drawNs = 0;
  qint64 totalNs = 0;
};

// Aggregates per-frame timings and prints periodic averages.
void logRenderProfileSample(const RenderProfileSample& sample) {
  static constexpr int kLogEveryFrames = 120;
  struct Totals {
    int frames = 0;
    qint64 captureNs = 0;
    qint64 staticNs = 0;
    qint64 dynamicNs = 0;
    qint64 uploadNs = 0;
    qint64 drawNs = 0;
    qint64 totalNs = 0;
  };

  static Totals totals;
  ++totals.frames;
  totals.captureNs += sample.captureNs;
  totals.staticNs += sample.staticNs;
  totals.dynamicNs += sample.dynamicNs;
  totals.uploadNs += sample.uploadNs;
  totals.drawNs += sample.drawNs;
  totals.totalNs += sample.totalNs;

  if (totals.frames < kLogEveryFrames) {
    return;
  }

  auto avgMs = [&](qint64 ns) -> double {
    return static_cast<double>(ns) / 1000000.0 / static_cast<double>(totals.frames);
  };
  qInfo().nospace() << "[PianoRollRhiRenderer] avg over " << totals.frames
                    << " frames: capture=" << avgMs(totals.captureNs) << "ms"
                    << ", static=" << avgMs(totals.staticNs) << "ms"
                    << ", dynamic=" << avgMs(totals.dynamicNs) << "ms"
                    << ", upload=" << avgMs(totals.uploadNs) << "ms"
                    << ", draw=" << avgMs(totals.drawNs) << "ms"
                    << ", total=" << avgMs(totals.totalNs) << "ms";
  totals = {};
}

// Precomputes white/black key topology once so frame builds avoid neighbor scans.
const KeyboardTopology& keyboardTopology() {
  static const KeyboardTopology topology = [] {
    KeyboardTopology t;
    std::array<bool, 257> seamDrawn{};

    auto centerUnitForKey = [](int key) -> float {
      return static_cast<float>(127 - key) + 0.5f;
    };
    auto addSeam = [&](float seamUnit) {
      const int seamId = std::clamp(static_cast<int>(std::lround(seamUnit * 2.0f)), 0, 256);
      if (seamDrawn[static_cast<size_t>(seamId)]) {
        return;
      }
      seamDrawn[static_cast<size_t>(seamId)] = true;
      t.seamUnits.push_back(seamUnit);
    };

    for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
      auto& keyTopology = t.keys[static_cast<size_t>(key)];
      keyTopology.centerUnit = centerUnitForKey(key);
      keyTopology.topSeamUnit = keyTopology.centerUnit - 0.5f;
      keyTopology.bottomSeamUnit = keyTopology.centerUnit + 0.5f;

      if (isBlackMidiKey(key)) {
        t.blackKeys.push_back(key);
        continue;
      }

      t.whiteKeys.push_back(key);

      int higherWhite = key + 1;
      while (higherWhite < PianoRollFrame::kMidiKeyCount && isBlackMidiKey(higherWhite)) {
        ++higherWhite;
      }
      int lowerWhite = key - 1;
      while (lowerWhite >= 0 && isBlackMidiKey(lowerWhite)) {
        --lowerWhite;
      }

      if (higherWhite < PianoRollFrame::kMidiKeyCount) {
        if (higherWhite == key + 1) {
          keyTopology.topSeamUnit = (keyTopology.centerUnit + centerUnitForKey(higherWhite)) * 0.5f;
        } else {
          keyTopology.topSeamUnit = centerUnitForKey(key + 1);
        }
      }

      if (lowerWhite >= 0) {
        if (lowerWhite == key - 1) {
          keyTopology.bottomSeamUnit = (keyTopology.centerUnit + centerUnitForKey(lowerWhite)) * 0.5f;
        } else {
          keyTopology.bottomSeamUnit = centerUnitForKey(key - 1);
        }
      }

      addSeam(keyTopology.topSeamUnit);
      addSeam(keyTopology.bottomSeamUnit);
    }

    return t;
  }();

  return topology;
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

  m_measureLabelSampler = m_rhi->newSampler(QRhiSampler::Linear,
                                            QRhiSampler::Linear,
                                            QRhiSampler::None,
                                            QRhiSampler::ClampToEdge,
                                            QRhiSampler::ClampToEdge);
  m_measureLabelSampler->create();

  m_staticBuffersUploaded = false;
  m_measureLabelAtlasDirty = true;
  m_inited = true;
}

void PianoRollRhiRenderer::releaseResources() {
  if (m_rhi) {
    m_rhi->makeThreadLocalNativeContextCurrent();
  }

  delete m_notePipeline;
  m_notePipeline = nullptr;

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

  delete m_noteInstanceBuffer;
  m_noteInstanceBuffer = nullptr;

  delete m_measureLabelAtlas;
  m_measureLabelAtlas = nullptr;

  delete m_measureLabelSampler;
  m_measureLabelSampler = nullptr;

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
  m_noteInstances.clear();
  m_dynamicFrontStart = 0;
  m_measureLabelAtlasDirty = true;
  m_measureLabelAtlasScale = 1.0f;
  m_measureLabelHeight = 0.0f;
  m_measureLabelGlyphs = {};
  m_hasStaticCacheKey = false;
  m_staticCacheKey = {};
  m_staticDataDirty = true;
  m_noteDataDirty = true;
  m_hasNoteDataKey = false;
  m_noteDataKey = {};
  m_rhi = nullptr;
}

void PianoRollRhiRenderer::renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target) {
  if (!cb || !m_rhi || !m_view || !target.renderTarget || !target.renderPassDesc || target.pixelSize.isEmpty()) {
    return;
  }

  const bool profileEnabled = pianoRollProfileEnabled();
  QElapsedTimer profileTimer;
  qint64 tCapture = 0;
  qint64 tStatic = 0;
  qint64 tDynamic = 0;
  qint64 tUpload = 0;
  if (profileEnabled) {
    profileTimer.start();
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
  const StaticCacheKey key = makeStaticCacheKey(frame, layout);
  const NoteDataKey noteKey = makeNoteDataKey(frame);
  if (!m_hasNoteDataKey ||
      noteKey.notesPtr != m_noteDataKey.notesPtr ||
      noteKey.trackColorsHash != m_noteDataKey.trackColorsHash ||
      noteKey.trackEnabledHash != m_noteDataKey.trackEnabledHash ||
      noteKey.noteBackgroundColor != m_noteDataKey.noteBackgroundColor) {
    rebuildNoteInstances(frame);
    m_noteDataKey = noteKey;
    m_hasNoteDataKey = true;
    m_noteDataDirty = true;
  }
  if (profileEnabled) {
    tCapture = profileTimer.nsecsElapsed();
  }

  QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
  ensureMeasureLabelAtlas(updates, frame.dpr);

  if (!m_hasStaticCacheKey || !staticCacheKeyEqual(key, m_staticCacheKey)) {
    buildStaticInstances(frame, layout);
    m_staticCacheKey = key;
    m_hasStaticCacheKey = true;
    m_staticDataDirty = true;
  }
  if (profileEnabled) {
    tStatic = profileTimer.nsecsElapsed();
  }
  // Dynamic overlays (active notes, scanline, progress) update every frame.
  buildDynamicInstances(frame, layout);
  if (profileEnabled) {
    tDynamic = profileTimer.nsecsElapsed();
  }

  ensurePipelines(target.renderPassDesc, std::max(1, target.sampleCount));
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
  std::array<float, 28> ubo{};
  std::memcpy(ubo.data(), mvp.constData(), kMat4Bytes);
  // Camera packs scroll and world-space scale factors for shader-side transforms.
  ubo[16] = layout.scrollX;
  ubo[17] = layout.scrollY;
  ubo[18] = layout.pixelsPerTick;
  ubo[19] = layout.pixelsPerKey;
  ubo[20] = layout.noteAreaLeft;
  ubo[21] = layout.noteAreaTop;
  ubo[22] = layout.noteAreaLeft + layout.noteAreaWidth;
  ubo[23] = layout.noteAreaTop + layout.noteAreaHeight;
  ubo[24] = frame.noteOutlineColor.redF();
  ubo[25] = frame.noteOutlineColor.greenF();
  ubo[26] = frame.noteOutlineColor.blueF();
  ubo[27] = frame.noteOutlineColor.alphaF();
  updates->updateDynamicBuffer(m_uniformBuffer, 0, kUniformBytes, ubo.data());

  if (m_staticDataDirty) {
    if (!m_staticBackInstances.empty()) {
      const int staticBackBytes = static_cast<int>(m_staticBackInstances.size() * sizeof(RectInstance));
      if (ensureInstanceBuffer(m_staticBackInstanceBuffer, staticBackBytes, 16384)) {
        updates->updateDynamicBuffer(m_staticBackInstanceBuffer,
                                     0,
                                     staticBackBytes,
                                     m_staticBackInstances.data());
      }
    }

    if (!m_staticFrontInstances.empty()) {
      const int staticFrontBytes = static_cast<int>(m_staticFrontInstances.size() * sizeof(RectInstance));
      if (ensureInstanceBuffer(m_staticFrontInstanceBuffer, staticFrontBytes, 4096)) {
        updates->updateDynamicBuffer(m_staticFrontInstanceBuffer,
                                     0,
                                     staticFrontBytes,
                                     m_staticFrontInstances.data());
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

  if (m_noteDataDirty) {
    if (!m_noteInstances.empty()) {
      const int noteBytes = static_cast<int>(m_noteInstances.size() * sizeof(NoteInstance));
      if (ensureInstanceBuffer(m_noteInstanceBuffer, noteBytes, 16384)) {
        updates->updateDynamicBuffer(m_noteInstanceBuffer, 0, noteBytes, m_noteInstances.data());
      }
    }
    m_noteDataDirty = false;
  }
  if (profileEnabled) {
    tUpload = profileTimer.nsecsElapsed();
  }

  cb->beginPass(target.renderTarget, frame.backgroundColor, {1.0f, 0}, updates);
  cb->setViewport(QRhiViewport(0,
                               0,
                               static_cast<float>(target.pixelSize.width()),
                               static_cast<float>(target.pixelSize.height())));

  auto drawRectInstances = [&](QRhiBuffer* buffer, int count, int firstInstance = 0) {
    if (!m_pipeline || !m_shaderBindings || !buffer || count <= 0) {
      return;
    }

    cb->setGraphicsPipeline(m_pipeline);
    cb->setShaderResources(m_shaderBindings);
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

  const int staticBackCount = static_cast<int>(m_staticBackInstances.size());
  const int staticFrontCount = static_cast<int>(m_staticFrontInstances.size());
  drawRectInstances(m_staticBackInstanceBuffer, staticBackCount);

  if (m_notePipeline && m_shaderBindings && m_noteInstanceBuffer && !m_noteInstances.empty()) {
    int noteFirstInstance = 0;
    int noteInstanceCount = static_cast<int>(m_noteInstances.size());
    if (frame.notes && !frame.notes->empty()) {
      const auto& notes = *frame.notes;
      const uint64_t maxDuration = std::max<uint64_t>(1, frame.maxNoteDurationTicks);
      const uint64_t searchStartTick = (layout.visibleStartTick > maxDuration)
                                           ? (layout.visibleStartTick - maxDuration)
                                           : 0;
      const uint64_t searchEndTick = layout.visibleEndTick + 1;

      const auto beginIt = std::lower_bound(
          notes.begin(),
          notes.end(),
          searchStartTick,
          [](const PianoRollFrame::Note& note, uint64_t tick) {
            return static_cast<uint64_t>(note.startTick) < tick;
          });
      const auto endIt = std::lower_bound(
          notes.begin(),
          notes.end(),
          searchEndTick,
          [](const PianoRollFrame::Note& note, uint64_t tick) {
            return static_cast<uint64_t>(note.startTick) < tick;
          });

      const int beginIndex = std::clamp(static_cast<int>(std::distance(notes.begin(), beginIt)),
                                        0,
                                        static_cast<int>(m_noteInstances.size()));
      const int endIndex = std::clamp(static_cast<int>(std::distance(notes.begin(), endIt)),
                                      beginIndex,
                                      static_cast<int>(m_noteInstances.size()));
      noteFirstInstance = beginIndex;
      noteInstanceCount = std::max(0, endIndex - beginIndex);
    }

    if (noteInstanceCount > 0) {
      cb->setGraphicsPipeline(m_notePipeline);
      cb->setShaderResources(m_shaderBindings);
      const QRhiCommandBuffer::VertexInput noteBindings[] = {
          {m_vertexBuffer, 0},
          {m_noteInstanceBuffer, 0},
      };
      cb->setVertexInput(0,
                         static_cast<int>(std::size(noteBindings)),
                         noteBindings,
                         m_indexBuffer,
                         0,
                         QRhiCommandBuffer::IndexUInt16);
      cb->drawIndexed(6, noteInstanceCount, 0, 0, noteFirstInstance);
    }
  }

  const int dynamicCount = static_cast<int>(m_dynamicInstances.size());
  const int dynamicFrontStart = std::clamp(m_dynamicFrontStart, 0, dynamicCount);
  drawRectInstances(m_dynamicInstanceBuffer, dynamicFrontStart, 0);
  drawRectInstances(m_staticFrontInstanceBuffer, staticFrontCount);
  drawRectInstances(m_dynamicInstanceBuffer, dynamicCount - dynamicFrontStart, dynamicFrontStart);

  cb->endPass();

  if (profileEnabled) {
    const qint64 tEnd = profileTimer.nsecsElapsed();
    logRenderProfileSample(RenderProfileSample{
        tCapture,
        tStatic - tCapture,
        tDynamic - tStatic,
        tUpload - tDynamic,
        tEnd - tUpload,
        tEnd,
    });
  }
}

bool PianoRollRhiRenderer::isBlackKey(int key) {
  return isBlackMidiKey(key);
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

// Hashes track-enabled state for note-instance cache invalidation on mute/solo changes.
uint64_t PianoRollRhiRenderer::hashTrackEnabled(const std::vector<uint8_t>& trackEnabled) {
  uint64_t hash = 1469598103934665603ULL;
  for (const uint8_t enabled : trackEnabled) {
    hash ^= static_cast<uint64_t>(enabled);
    hash *= 1099511628211ULL;
  }
  hash ^= static_cast<uint64_t>(trackEnabled.size());
  hash *= 1099511628211ULL;
  return hash;
}

uint32_t PianoRollRhiRenderer::colorKey(const QColor& color) {
  return color.rgba();
}

// Recreates draw pipelines when render-target sample state changes.
void PianoRollRhiRenderer::ensurePipelines(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount) {
  if (!renderPassDesc) {
    return;
  }

  if (m_pipeline && m_notePipeline && m_shaderBindings &&
      m_outputRenderPass == renderPassDesc && m_sampleCount == sampleCount) {
    return;
  }

  // Render pass/sample count changes require a fresh pipeline.
  delete m_notePipeline;
  m_notePipeline = nullptr;

  delete m_pipeline;
  m_pipeline = nullptr;

  delete m_shaderBindings;
  m_shaderBindings = nullptr;

  m_outputRenderPass = renderPassDesc;
  m_sampleCount = sampleCount;

  m_shaderBindings = m_rhi->newShaderResourceBindings();
  m_shaderBindings->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0,
                                               QRhiShaderResourceBinding::VertexStage |
                                                   QRhiShaderResourceBinding::FragmentStage,
                                               m_uniformBuffer),
      QRhiShaderResourceBinding::sampledTexture(1,
                                                QRhiShaderResourceBinding::FragmentStage,
                                                m_measureLabelAtlas,
                                                m_measureLabelSampler),
  });
  m_shaderBindings->create();

  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::One;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

  QShader rectVertexShader = loadShader(":/shaders/pianorollquad.vert.qsb");
  QShader rectFragmentShader = loadShader(":/shaders/pianorollquad.frag.qsb");
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
      {1, 4, QRhiVertexInputAttribute::Float4, 12 * static_cast<int>(sizeof(float))},
      {1, 5, QRhiVertexInputAttribute::Float2, 16 * static_cast<int>(sizeof(float))},
  });

  m_pipeline = m_rhi->newGraphicsPipeline();
  m_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
  m_pipeline->setSampleCount(m_sampleCount);
  m_pipeline->setShaderStages({
      {QRhiShaderStage::Vertex, rectVertexShader},
      {QRhiShaderStage::Fragment, rectFragmentShader},
  });
  m_pipeline->setShaderResourceBindings(m_shaderBindings);
  m_pipeline->setVertexInputLayout(rectInputLayout);
  m_pipeline->setTargetBlends({blend});
  m_pipeline->setRenderPassDescriptor(renderPassDesc);
  m_pipeline->create();

  QShader noteVertexShader = loadShader(":/shaders/pianorollnote.vert.qsb");
  QShader noteFragmentShader = loadShader(":/shaders/pianorollnote.frag.qsb");
  QRhiVertexInputLayout noteInputLayout;
  noteInputLayout.setBindings({
      {2 * static_cast<int>(sizeof(float))},
      {static_cast<int>(sizeof(NoteInstance)), QRhiVertexInputBinding::PerInstance},
  });
  noteInputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {1, 1, QRhiVertexInputAttribute::Float4, 0},
      {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
  });

  m_notePipeline = m_rhi->newGraphicsPipeline();
  m_notePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
  m_notePipeline->setSampleCount(m_sampleCount);
  m_notePipeline->setShaderStages({
      {QRhiShaderStage::Vertex, noteVertexShader},
      {QRhiShaderStage::Fragment, noteFragmentShader},
  });
  m_notePipeline->setShaderResourceBindings(m_shaderBindings);
  m_notePipeline->setVertexInputLayout(noteInputLayout);
  m_notePipeline->setTargetBlends({blend});
  m_notePipeline->setRenderPassDescriptor(renderPassDesc);
  m_notePipeline->create();
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

// Builds/uploads a tiny font atlas used by measure and keyboard labels.
void PianoRollRhiRenderer::ensureMeasureLabelAtlas(QRhiResourceUpdateBatch* updates, float dpr) {
  if (!m_rhi || !updates || !m_measureLabelSampler) {
    return;
  }

  const float atlasScale = std::max(1.0f, dpr);
  if (std::abs(atlasScale - m_measureLabelAtlasScale) > 0.01f) {
    m_measureLabelAtlasScale = atlasScale;
    m_measureLabelAtlasDirty = true;
  }

  if (!m_measureLabelAtlas) {
    // Keep binding 1 valid even before the real atlas is generated.
    m_measureLabelAtlas = m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
    if (!m_measureLabelAtlas || !m_measureLabelAtlas->create()) {
      delete m_measureLabelAtlas;
      m_measureLabelAtlas = nullptr;
      return;
    }
    QImage whitePixel(1, 1, QImage::Format_RGBA8888);
    whitePixel.fill(Qt::white);
    updates->uploadTexture(m_measureLabelAtlas, whitePixel);
  }

  if (!m_measureLabelAtlasDirty) {
    return;
  }

  QFont font = m_view ? m_view->font() : QFont{};
  font.setPixelSize(std::max(1, static_cast<int>(std::lround(kMeasureLabelFontPixelSize * atlasScale))));
  QFontMetrics metrics(font);

  std::array<bool, 128> hasGlyph{};
  const char* atlasChars = "0123456789C-";
  for (const char* ch = atlasChars; *ch != '\0'; ++ch) {
    const unsigned char code = static_cast<unsigned char>(*ch);
    hasGlyph[code] = true;
  }

  const int glyphHeight = std::max(1, metrics.height());
  const int atlasHeight = glyphHeight + (2 * kMeasureLabelPaddingPx);
  int atlasWidth = kMeasureLabelPaddingPx;
  std::array<int, 128> advances{};
  for (int code = 0; code < 128; ++code) {
    if (!hasGlyph[static_cast<size_t>(code)]) {
      continue;
    }
    const QString text(QChar(static_cast<char16_t>(code)));
    const int advance = std::max(1, metrics.horizontalAdvance(text));
    advances[static_cast<size_t>(code)] = advance;
    atlasWidth += advance + kMeasureLabelPaddingPx;
  }

  QImage atlas(std::max(1, atlasWidth), atlasHeight, QImage::Format_RGBA8888);
  atlas.fill(Qt::transparent);
  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setPen(Qt::white);
  painter.setFont(font);

  const qreal baselineY = static_cast<qreal>(kMeasureLabelPaddingPx + metrics.ascent());
  int cursorX = kMeasureLabelPaddingPx;
  m_measureLabelGlyphs = {};
  for (int code = 0; code < 128; ++code) {
    if (!hasGlyph[static_cast<size_t>(code)]) {
      continue;
    }
    const QString text(QChar(static_cast<char16_t>(code)));
    painter.drawText(QPointF(static_cast<qreal>(cursorX), baselineY), text);

    const int advance = advances[static_cast<size_t>(code)];
    // Pad UV bounds by 1px so antialiased edges are not clipped.
    const int left = std::max(0, cursorX - 1);
    const int right = std::min(atlas.width(), cursorX + advance + 1);
    LabelGlyph& glyph = m_measureLabelGlyphs[static_cast<size_t>(code)];
    glyph.u0 = static_cast<float>(left) / static_cast<float>(atlas.width());
    glyph.v0 = static_cast<float>(kMeasureLabelPaddingPx) / static_cast<float>(atlas.height());
    glyph.u1 = static_cast<float>(right) / static_cast<float>(atlas.width());
    glyph.v1 = static_cast<float>(kMeasureLabelPaddingPx + glyphHeight) / static_cast<float>(atlas.height());
    glyph.width = static_cast<float>(std::max(1, right - left)) / atlasScale;
    glyph.advance = static_cast<float>(advance + 1) / atlasScale;
    cursorX += advance + kMeasureLabelPaddingPx;
  }
  painter.end();

  if (m_measureLabelAtlas->pixelSize() != atlas.size()) {
    delete m_measureLabelAtlas;
    m_measureLabelAtlas = m_rhi->newTexture(QRhiTexture::RGBA8, atlas.size(), 1);
    if (!m_measureLabelAtlas || !m_measureLabelAtlas->create()) {
      delete m_measureLabelAtlas;
      m_measureLabelAtlas = nullptr;
      return;
    }
  }

  updates->uploadTexture(m_measureLabelAtlas, atlas);
  m_measureLabelHeight = static_cast<float>(glyphHeight) / atlasScale;
  m_measureLabelAtlasDirty = false;

  if (m_shaderBindings) {
    m_shaderBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage |
                                                     QRhiShaderResourceBinding::FragmentStage,
                                                 m_uniformBuffer),
        QRhiShaderResourceBinding::sampledTexture(1,
                                                  QRhiShaderResourceBinding::FragmentStage,
                                                  m_measureLabelAtlas,
                                                  m_measureLabelSampler),
    });
    m_shaderBindings->create();
  }
}

const PianoRollRhiRenderer::LabelGlyph* PianoRollRhiRenderer::glyphForLabelChar(QChar ch) const {
  const int code = ch.unicode();
  if (code < 0 || code >= static_cast<int>(m_measureLabelGlyphs.size())) {
    return nullptr;
  }
  const LabelGlyph& glyph = m_measureLabelGlyphs[static_cast<size_t>(code)];
  return (glyph.width > 0.0f) ? &glyph : nullptr;
}

float PianoRollRhiRenderer::labelTextWidth(const QString& text, float labelHeight) const {
  if (text.isEmpty() || m_measureLabelHeight <= 0.0f || labelHeight <= 0.0f) {
    return 0.0f;
  }

  const float scale = labelHeight / m_measureLabelHeight;
  float maxRight = 0.0f;
  float cursorX = 0.0f;
  for (const QChar ch : text) {
    const LabelGlyph* glyph = glyphForLabelChar(ch);
    if (!glyph) {
      continue;
    }
    maxRight = std::max(maxRight, cursorX + (glyph->width * scale));
    cursorX += glyph->advance * scale;
  }
  return maxRight;
}

void PianoRollRhiRenderer::appendLabelText(const QString& text,
                                           float x,
                                           float y,
                                           float labelHeight,
                                           const QColor& color) {
  if (text.isEmpty() || labelHeight <= 0.0f || m_measureLabelHeight <= 0.0f) {
    return;
  }

  const float scale = labelHeight / m_measureLabelHeight;
  float cursorX = x;
  for (const QChar ch : text) {
    const LabelGlyph* glyph = glyphForLabelChar(ch);
    if (!glyph) {
      continue;
    }

    appendRect(m_dynamicInstances,
               cursorX,
               y,
               glyph->width * scale,
               labelHeight,
               color,
               LineStyle::LabelText,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               // For LabelText, coordMode packs glyph UV (u0,v0,u1,v1).
               glyph->u0,
               glyph->v0,
               glyph->u1,
               glyph->v1);
    cursorX += glyph->advance * scale;
  }
}

// Emits top-bar measure-number glyph quads using the cached font atlas.
void PianoRollRhiRenderer::appendMeasureNumberOverlays(const PianoRollFrame::Data& frame,
                                                       const Layout& layout) {
  if (layout.noteAreaWidth <= 0.0f || layout.topBarHeight <= 0.0f ||
      !m_measureLabelAtlas || m_measureLabelHeight <= 0.0f) {
    return;
  }

  struct MeasureSegment {
    uint64_t start = 0;
    uint64_t end = 0;
    uint32_t measureTicks = 1;
    uint64_t firstMeasureIndex = 0;
  };

  std::vector<PianoRollFrame::TimeSignature> signatures;
  if (frame.timeSignatures && !frame.timeSignatures->empty()) {
    signatures = *frame.timeSignatures;
  } else {
    signatures.push_back({0, 4, 4});
  }
  std::sort(signatures.begin(), signatures.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.tick < rhs.tick;
  });

  const uint64_t totalTickEnd = static_cast<uint64_t>(std::max(1, frame.totalTicks)) + 1;
  std::vector<MeasureSegment> segments;
  segments.reserve(signatures.size());

  uint64_t runningMeasureIndex = 0;
  for (size_t i = 0; i < signatures.size(); ++i) {
    const auto& sig = signatures[i];
    const uint64_t segStart = sig.tick;
    uint64_t segEnd = totalTickEnd;
    if (i + 1 < signatures.size()) {
      segEnd = std::min<uint64_t>(segEnd, std::max<uint64_t>(segStart, signatures[i + 1].tick));
    }
    if (segStart >= totalTickEnd || segEnd <= segStart) {
      continue;
    }

    const uint32_t numerator = std::max<uint32_t>(1, sig.numerator);
    const uint32_t denominator = std::max<uint32_t>(1, sig.denominator);
    const uint32_t beatTicks = std::max<uint32_t>(
        1,
        static_cast<uint32_t>(std::lround((static_cast<double>(frame.ppqn) * 4.0) /
                                          static_cast<double>(denominator))));
    const uint64_t measureTicks64 = static_cast<uint64_t>(beatTicks) * numerator;
    const uint32_t measureTicks = static_cast<uint32_t>(std::min<uint64_t>(
        std::numeric_limits<uint32_t>::max(),
        std::max<uint64_t>(1, measureTicks64)));

    const uint64_t spanTicks = segEnd - segStart;
    const uint64_t measureCount = ((spanTicks - 1) / measureTicks) + 1;
    segments.push_back({segStart, segEnd, measureTicks, runningMeasureIndex});
    runningMeasureIndex += measureCount;
  }
  if (segments.empty()) {
    return;
  }

  static constexpr float kMinMeasureSpacingForLabelsPx = 22.0f;
  static constexpr float kTargetMeasureLabelSpacingPx = 44.0f;
  static constexpr int kMaxVisibleLabels = 96;
  static constexpr float kLabelOffsetXPx = 3.0f;

  const uint64_t visibleStartTick = layout.visibleStartTick;
  const uint64_t visibleEndTick = layout.visibleEndTick + 1;
  float minMeasureSpacingPx = std::numeric_limits<float>::max();
  for (const MeasureSegment& seg : segments) {
    if (seg.end <= visibleStartTick || seg.start >= visibleEndTick) {
      continue;
    }
    minMeasureSpacingPx = std::min(minMeasureSpacingPx,
                                   static_cast<float>(seg.measureTicks) * layout.pixelsPerTick);
  }
  if (!std::isfinite(minMeasureSpacingPx) || minMeasureSpacingPx < kMinMeasureSpacingForLabelsPx) {
    return;
  }

  const uint64_t labelStep = std::max<uint64_t>(
      1,
      // At dense zoom levels, draw every Nth measure to cap draw cost.
      static_cast<uint64_t>(std::ceil(kTargetMeasureLabelSpacingPx / minMeasureSpacingPx)));
  const float labelHeight = std::clamp(m_measureLabelHeight,
                                       7.0f,
                                       std::max(7.0f, layout.topBarHeight - 2.0f));
  const float labelY = std::clamp(layout.topBarHeight * 0.16f,
                                  1.0f,
                                  std::max(1.0f, layout.topBarHeight - labelHeight - 1.0f));
  const float noteAreaRight = layout.noteAreaLeft + layout.noteAreaWidth;
  QColor labelColor = frame.measureLineColor.lighter(116);
  labelColor.setAlpha(std::clamp(labelColor.alpha() + 84, 96, 220));

  int visibleLabelCount = 0;
  for (const MeasureSegment& seg : segments) {
    if (seg.end <= visibleStartTick || seg.start >= visibleEndTick) {
      continue;
    }

    uint64_t firstMeasureInSeg = 0;
    if (visibleStartTick > seg.start) {
      firstMeasureInSeg = (visibleStartTick - seg.start + seg.measureTicks - 1) / seg.measureTicks;
    }

    for (uint64_t localMeasure = firstMeasureInSeg;; ++localMeasure) {
      const uint64_t tick = seg.start + (localMeasure * seg.measureTicks);
      if (tick >= seg.end || tick >= visibleEndTick) {
        break;
      }

      const uint64_t measureIndex = seg.firstMeasureIndex + localMeasure;
      if ((measureIndex % labelStep) != 0) {
        continue;
      }

      const int measureNumber = static_cast<int>(
          std::min<uint64_t>(measureIndex + 1, static_cast<uint64_t>(std::numeric_limits<int>::max())));
      const QString measureText = QString::number(measureNumber);
      const float labelWidth = labelTextWidth(measureText, labelHeight);
      if (labelWidth <= 0.0f) {
        continue;
      }

      float labelX = layout.noteAreaLeft + (static_cast<float>(tick) * layout.pixelsPerTick) - layout.scrollX;
      labelX += kLabelOffsetXPx;
      if (labelX < layout.noteAreaLeft + 1.0f || labelX + labelWidth > noteAreaRight - 1.0f) {
        continue;
      }

      appendLabelText(measureText, labelX, labelY, labelHeight, labelColor);

      ++visibleLabelCount;
      if (visibleLabelCount >= kMaxVisibleLabels) {
        return;
      }
    }
  }
}

void PianoRollRhiRenderer::appendPianoCKeyLabels(const PianoRollFrame::Data& frame,
                                                 const Layout& layout) {
  if (layout.keyboardWidth <= 0.0f || layout.pixelsPerKey < 7.0f ||
      m_measureLabelHeight <= 0.0f || !m_measureLabelAtlas) {
    return;
  }

  const KeyboardTopology& topology = keyboardTopology();
  QColor labelColor = frame.blackKeyColor;
  labelColor.setAlpha(166);

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if ((key % 12) != 0) {
      continue;
    }

    const KeyboardKeyTopology& keyTopo = topology.keys[static_cast<size_t>(key)];
    const float keyTop = layout.noteAreaTop + (keyTopo.topSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float keyBottom = layout.noteAreaTop + (keyTopo.bottomSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float keyHeight = keyBottom - keyTop;
    if (keyHeight <= 8.0f) {
      continue;
    }

    const int octave = (key / 12) - 1;
    const QString label = QStringLiteral("C%1").arg(octave);
    const float labelHeight = std::clamp(m_measureLabelHeight,
                                         7.0f,
                                         std::max(7.0f, keyHeight - 2.0f));
    const float labelWidth = labelTextWidth(label, labelHeight);
    if (labelWidth <= 0.0f || labelWidth > layout.keyboardWidth - 4.0f) {
      continue;
    }

    const float labelX = std::max(1.0f, layout.keyboardWidth - labelWidth - 4.0f);
    const float labelY = keyBottom - labelHeight - 1.5f;
    appendLabelText(label, labelX, labelY, labelHeight, labelColor);
  }
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
                                                                               const Layout& layout) const {
  StaticCacheKey key;
  key.viewSize = QSize(layout.viewWidth, layout.viewHeight);
  key.totalTicks = std::max(1, frame.totalTicks);
  key.ppqn = std::max(1, frame.ppqn);
  key.keyboardWidth = static_cast<int>(std::lround(layout.keyboardWidth));
  key.topBarHeight = static_cast<int>(std::lround(layout.topBarHeight));
  key.timeSigPtr = reinterpret_cast<uint64_t>(frame.timeSignatures.get());
  key.noteBackgroundColor = colorKey(frame.noteBackgroundColor);
  key.keyboardBackgroundColor = colorKey(frame.keyboardBackgroundColor);
  key.topBarBackgroundColor = colorKey(frame.topBarBackgroundColor);
  key.measureLineColor = colorKey(frame.measureLineColor);
  key.beatLineColor = colorKey(frame.beatLineColor);
  key.keySeparatorColor = colorKey(frame.keySeparatorColor);
  key.whiteKeyColor = colorKey(frame.whiteKeyColor);
  key.blackKeyColor = colorKey(frame.blackKeyColor);
  key.whiteKeyRowColor = colorKey(frame.whiteKeyRowColor);
  key.blackKeyRowColor = colorKey(frame.blackKeyRowColor);
  key.dividerColor = colorKey(frame.dividerColor);
  return key;
}

bool PianoRollRhiRenderer::staticCacheKeyEqual(const StaticCacheKey& lhs, const StaticCacheKey& rhs) {
  return lhs.viewSize == rhs.viewSize &&
         lhs.totalTicks == rhs.totalTicks &&
         lhs.ppqn == rhs.ppqn &&
         lhs.keyboardWidth == rhs.keyboardWidth &&
         lhs.topBarHeight == rhs.topBarHeight &&
         lhs.timeSigPtr == rhs.timeSigPtr &&
         lhs.noteBackgroundColor == rhs.noteBackgroundColor &&
         lhs.keyboardBackgroundColor == rhs.keyboardBackgroundColor &&
         lhs.topBarBackgroundColor == rhs.topBarBackgroundColor &&
         lhs.measureLineColor == rhs.measureLineColor &&
         lhs.beatLineColor == rhs.beatLineColor &&
         lhs.keySeparatorColor == rhs.keySeparatorColor &&
         lhs.whiteKeyColor == rhs.whiteKeyColor &&
         lhs.blackKeyColor == rhs.blackKeyColor &&
         lhs.whiteKeyRowColor == rhs.whiteKeyRowColor &&
         lhs.blackKeyRowColor == rhs.blackKeyRowColor &&
         lhs.dividerColor == rhs.dividerColor;
}

// Builds a compact cache key for note-instance uploads.
PianoRollRhiRenderer::NoteDataKey PianoRollRhiRenderer::makeNoteDataKey(const PianoRollFrame::Data& frame) const {
  NoteDataKey key;
  key.notesPtr = reinterpret_cast<uint64_t>(frame.notes.get());
  if (frame.trackColors) {
    key.trackColorsHash = hashTrackColors(*frame.trackColors);
  }
  if (frame.trackEnabled) {
    key.trackEnabledHash = hashTrackEnabled(*frame.trackEnabled);
  }
  key.noteBackgroundColor = colorKey(frame.noteBackgroundColor);
  return key;
}

// Converts sequence notes into persistent GPU note instances.
void PianoRollRhiRenderer::rebuildNoteInstances(const PianoRollFrame::Data& frame) {
  m_noteInstances.clear();
  if (!frame.notes || frame.notes->empty()) {
    return;
  }

  m_noteInstances.reserve(frame.notes->size());
  const auto* trackColors = frame.trackColors.get();
  const auto* trackEnabled = frame.trackEnabled.get();
  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackColors && trackIndex >= 0 && trackIndex < static_cast<int>(trackColors->size())) {
      return (*trackColors)[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };
  const auto isTrackEnabled = [&](int trackIndex) -> bool {
    return !trackEnabled ||
           trackIndex < 0 ||
           trackIndex >= static_cast<int>(trackEnabled->size()) ||
           (*trackEnabled)[static_cast<size_t>(trackIndex)] != 0;
  };

  for (const PianoRollFrame::Note& note : *frame.notes) {
    const bool trackEnabled = isTrackEnabled(note.trackIndex);
    QColor fillColor;
    if (trackEnabled) {
      fillColor = colorForTrack(note.trackIndex);
      fillColor.setAlpha(188);
    } else {
      const QColor trackColor = colorForTrack(note.trackIndex);
      const QColor bgColor = frame.noteBackgroundColor;
      static constexpr float kTrackMix = 0.40f;
      fillColor = QColor::fromRgbF(
          (bgColor.redF() * (1.0f - kTrackMix)) + (trackColor.redF() * kTrackMix),
          (bgColor.greenF() * (1.0f - kTrackMix)) + (trackColor.greenF() * kTrackMix),
          (bgColor.blueF() * (1.0f - kTrackMix)) + (trackColor.blueF() * kTrackMix),
          0.78f);
    }

    m_noteInstances.push_back(NoteInstance{
        static_cast<float>(note.startTick),
        static_cast<float>(std::max<uint32_t>(1, note.duration)),
        static_cast<float>(note.key),
        trackEnabled ? 1.0f : 0.0f,
        fillColor.redF(),
        fillColor.greenF(),
        fillColor.blueF(),
        fillColor.alphaF(),
    });
  }
}

void PianoRollRhiRenderer::buildStaticInstances(const PianoRollFrame::Data& frame, const Layout& layout) {
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
  const KeyboardTopology& topology = keyboardTopology();
  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    const float keyUnit = 127.0f - static_cast<float>(key);

    const QColor rowColor = isBlackKey(key) ? frame.blackKeyRowColor : frame.whiteKeyRowColor;
    appendRect(m_staticBackInstances,
               layout.noteAreaLeft,
               keyUnit,
               layout.noteAreaWidth,
               1.0f,
               rowColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f);

    QColor rowSep = frame.keySeparatorColor;
    rowSep.setAlpha(std::max(24, rowSep.alpha()));
    appendRect(m_staticBackInstances,
               layout.noteAreaLeft,
               keyUnit,
               layout.noteAreaWidth,
               1.0f,
               rowSep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               0.0f);
  }

  // Draw the top bar after scrolling lane bands so it remains visually opaque.
  QColor topBarBase = frame.topBarBackgroundColor;
  topBarBase.setAlpha(255);
  appendRect(m_staticBackInstances,
             layout.noteAreaLeft,
             0.0f,
             layout.noteAreaWidth,
             layout.topBarHeight,
             topBarBase,
             LineStyle::TopBarGradient,
             1.10f,
             0.86f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f);

  // Emit one procedural grid rect per time-signature segment instead of per-line quads.
  std::vector<PianoRollFrame::TimeSignature> signatures;
  if (frame.timeSignatures && !frame.timeSignatures->empty()) {
    signatures = *frame.timeSignatures;
  } else {
    signatures.push_back({0, 4, 4});
  }
  std::sort(signatures.begin(), signatures.end(), [](const auto& a, const auto& b) {
    return a.tick < b.tick;
  });

  const uint64_t totalTickEnd = static_cast<uint64_t>(std::max(1, frame.totalTicks)) + 1;
  // Encodes beat/measure spacing in instance params; the fragment shader draws the vertical lines.
  const auto appendGridSegment = [&](uint64_t segStart,
                                     uint64_t segEnd,
                                     uint32_t beatTicks,
                                     uint32_t measureTicks) {
    if (segEnd <= segStart || beatTicks == 0 || measureTicks == 0) {
      return;
    }

    const float segX = static_cast<float>(segStart);
    const float segW = static_cast<float>(segEnd - segStart);
    const float beatTicksF = static_cast<float>(beatTicks);
    const float measureTicksF = static_cast<float>(measureTicks);
    const float originTickF = static_cast<float>(segStart);

    appendRect(m_staticBackInstances,
               segX,
               layout.noteAreaTop,
               segW,
               layout.noteAreaHeight,
               frame.measureLineColor,
               LineStyle::GridMeasure,
               beatTicksF,
               measureTicksF,
               originTickF,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f);
    appendRect(m_staticBackInstances,
               segX,
               layout.noteAreaTop,
               segW,
               layout.noteAreaHeight,
               frame.beatLineColor,
               LineStyle::GridBeat,
               beatTicksF,
               measureTicksF,
               originTickF,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f);

    QColor topMeasure = frame.measureLineColor;
    topMeasure.setAlpha(std::min(255, topMeasure.alpha() + 35));
    const float topBarInset = std::clamp(layout.topBarHeight * 0.20f + 5.0f,
                                         2.0f,
                                         std::max(2.0f, layout.topBarHeight - 2.0f));
    appendRect(m_staticBackInstances,
               segX,
               topBarInset,
               segW,
               layout.topBarHeight - topBarInset,
               topMeasure,
               LineStyle::GridMeasure,
               beatTicksF,
               measureTicksF,
               originTickF,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f);
  };

  for (size_t i = 0; i < signatures.size(); ++i) {
    const auto& sig = signatures[i];
    const uint32_t numerator = std::max<uint32_t>(1, sig.numerator);
    const uint32_t denominator = std::max<uint32_t>(1, sig.denominator);
    const uint32_t beatTicks = std::max<uint32_t>(
        1,
        static_cast<uint32_t>(std::lround((static_cast<double>(frame.ppqn) * 4.0) /
                                          static_cast<double>(denominator))));
    const uint64_t measureTicks64 = static_cast<uint64_t>(beatTicks) * numerator;
    const uint32_t measureTicks = static_cast<uint32_t>(std::min<uint64_t>(
        std::numeric_limits<uint32_t>::max(),
        std::max<uint64_t>(1, measureTicks64)));

    const uint64_t segStart = sig.tick;
    uint64_t segEnd = totalTickEnd;
    if (i + 1 < signatures.size()) {
      segEnd = std::min<uint64_t>(segEnd, std::max<uint64_t>(segStart, signatures[i + 1].tick));
    }
    appendGridSegment(segStart, segEnd, beatTicks, measureTicks);
  }

  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeightUnits = 1.0f;
  const float blackTopInsetLeftPx = 1.0f;
  const float blackTopInsetRightPx = std::clamp(blackKeyWidth * 0.12f, 2.0f, 6.0f);
  const float blackTopInsetYUnits = 0.20f;
  const float blackTopSheenCurve = 6.0f;
  for (int key : topology.whiteKeys) {
    const KeyboardKeyTopology& keyTopology = topology.keys[static_cast<size_t>(key)];
    const float keyTopUnit = keyTopology.topSeamUnit;
    const float keyBottomUnit = keyTopology.bottomSeamUnit;
    if (keyBottomUnit - keyTopUnit <= 0.001f) {
      continue;
    }

    appendRect(m_staticBackInstances,
               0.0f,
               keyTopUnit,
               layout.keyboardWidth,
               keyBottomUnit - keyTopUnit,
               frame.whiteKeyColor,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f);
  }

  QColor keySep = frame.keySeparatorColor;
  keySep.setAlpha(std::max(56, keySep.alpha()));
  for (float seamUnit : topology.seamUnits) {
    appendRect(m_staticBackInstances,
               0.0f,
               seamUnit,
               layout.keyboardWidth,
               1.0f,
               keySep,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               0.0f);
  }

  for (int key : topology.blackKeys) {
    const float blackYUnit = topology.keys[static_cast<size_t>(key)].centerUnit - (blackKeyHeightUnits * 0.5f);
    QColor blackBase = frame.blackKeyColor.darker(126);
    blackBase.setAlpha(255);

    appendRect(m_staticFrontInstances,
               0.0f,
               blackYUnit,
               blackKeyWidth,
               blackKeyHeightUnits,
               blackBase,
               LineStyle::Solid,
               0.0f,
               0.0f,
               0.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f);

    QColor blackTop = frame.blackKeyColor.lighter(114);
    blackTop.setAlpha(236);
    const float innerW = std::max(1.0f, blackKeyWidth - blackTopInsetLeftPx - blackTopInsetRightPx);
    const float innerHUnits = std::max(0.0f, blackKeyHeightUnits - (2.0f * blackTopInsetYUnits));
    const float innerX = blackTopInsetLeftPx;
    const float innerYUnit = blackYUnit + blackTopInsetYUnits;
    appendRect(m_staticFrontInstances,
               innerX,
               innerYUnit,
               innerW,
               std::max(0.0f, innerHUnits),
               blackTop,
               LineStyle::HorizontalGradient,
               0.82f,
               1.05f,
               blackTopSheenCurve,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
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

  appendMeasureNumberOverlays(frame, layout);

  const auto* trackColors = frame.trackColors.get();
  const auto* trackEnabled = frame.trackEnabled.get();
  const auto colorForTrack = [&](int trackIndex) -> QColor {
    if (trackColors && trackIndex >= 0 && trackIndex < static_cast<int>(trackColors->size())) {
      return (*trackColors)[static_cast<size_t>(trackIndex)];
    }
    return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
  };
  const auto isTrackEnabled = [&](int trackIndex) -> bool {
    return !trackEnabled ||
           trackIndex < 0 ||
           trackIndex >= static_cast<int>(trackEnabled->size()) ||
           (*trackEnabled)[static_cast<size_t>(trackIndex)] != 0;
  };

  if (frame.activeNotes && !frame.activeNotes->empty()) {
    for (const PianoRollFrame::Note& note : *frame.activeNotes) {
      if (!isTrackEnabled(note.trackIndex)) {
        continue;
      }
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
    QColor headColor = frame.playbackActive ? QColor(235, 90, 75) : QColor(140, 70, 67);
    headColor.setAlpha(255);
    const float headHeight = std::clamp(layout.topBarHeight * 0.5f,
                                        6.0f,
                                        std::max(6.0f, layout.topBarHeight - 2.0f));
    const float headWidth = std::max(14.0f, headHeight * 1.15f);
    appendRect(m_dynamicInstances,
               currentX - (headWidth * 0.5f),
               0.0f,
               headWidth,
               headHeight,
               headColor,
               LineStyle::TriangleDown,
               1.24f,
               1.00f);
  }

  if (frame.selectionRectVisible && frame.selectionRectW > 0.5f && frame.selectionRectH > 0.5f) {
    const float rawLeft = frame.selectionRectX;
    const float rawTop = frame.selectionRectY;
    const float rawW = frame.selectionRectW;
    const float rawH = frame.selectionRectH;
    const float noteAreaLeft = layout.noteAreaLeft;
    const float noteAreaTop = layout.noteAreaTop;
    const float noteAreaRight = layout.noteAreaLeft + layout.noteAreaWidth;
    const float noteAreaBottom = layout.noteAreaTop + layout.noteAreaHeight;
    // Clip each marquee segment manually so edges vanish correctly when out of view.
    const auto appendClippedToNoteArea =
        [&](float x, float y, float w, float h, const QColor& color) {
          const float clippedLeft = std::max(x, noteAreaLeft);
          const float clippedTop = std::max(y, noteAreaTop);
          const float clippedRight = std::min(x + w, noteAreaRight);
          const float clippedBottom = std::min(y + h, noteAreaBottom);
          appendRect(m_dynamicInstances,
                     clippedLeft,
                     clippedTop,
                     clippedRight - clippedLeft,
                     clippedBottom - clippedTop,
                     color);
        };

    appendClippedToNoteArea(rawLeft, rawTop, rawW, rawH, frame.selectionRectFillColor);

    const float edge = 1.0f;
    appendClippedToNoteArea(rawLeft, rawTop, rawW, edge, frame.selectionRectOutlineColor);
    appendClippedToNoteArea(rawLeft, rawTop + rawH - edge, rawW, edge, frame.selectionRectOutlineColor);
    appendClippedToNoteArea(rawLeft, rawTop, edge, rawH, frame.selectionRectOutlineColor);
    appendClippedToNoteArea(rawLeft + rawW - edge, rawTop, edge, rawH, frame.selectionRectOutlineColor);
  }

  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, layout.pixelsPerKey);
  const KeyboardTopology& topology = keyboardTopology();
  for (int key : topology.whiteKeys) {
    const KeyboardKeyTopology& keyTopology = topology.keys[static_cast<size_t>(key)];
    const float keyTop = layout.noteAreaTop + (keyTopology.topSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float keyBottom = layout.noteAreaTop + (keyTopology.bottomSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float clippedTop = std::max(layout.noteAreaTop, keyTop);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, keyBottom);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0 || !isTrackEnabled(activeTrack)) {
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

  // Draw C octave labels above white-key highlights.
  appendPianoCKeyLabels(frame, layout);

  // Black-key highlights must render above the static black key faces.
  m_dynamicFrontStart = static_cast<int>(m_dynamicInstances.size());
  for (int key : topology.blackKeys) {
    const float centerY = layout.noteAreaTop + (topology.keys[static_cast<size_t>(key)].centerUnit * layout.pixelsPerKey) - layout.scrollY;
    const float blackY = centerY - (blackKeyHeight * 0.5f);
    const float clippedTop = std::max(layout.noteAreaTop, blackY);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, blackY + blackKeyHeight);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0 || !isTrackEnabled(activeTrack)) {
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

  // Keep note quads inside the scrollable note area (not keyboard or top bar).
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
                                      float scrollMulY,
                                      float posModeX,
                                      float posModeY,
                                      float sizeModeX,
                                      float sizeModeY) {
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
      posModeX,
      posModeY,
      sizeModeX,
      sizeModeY,
      scrollMulX,
      scrollMulY,
  });
}
