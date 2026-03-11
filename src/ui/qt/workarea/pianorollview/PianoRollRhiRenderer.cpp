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
#include <unordered_map>
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
static constexpr int kUniformBytes = (16 + 4 + 4 + 4 + 4) * sizeof(float);
static constexpr int kMeasureLabelFontPixelSize = 11;
static constexpr int kMeasureLabelPaddingPx = 2;
static constexpr float kActiveLaserAuraPadPx = 72.0f;

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
  return true;
  // static const bool enabled = qEnvironmentVariableIntValue("VGMTRANS_PROFILE_PIANOROLL") > 0;
  // return enabled;
}

struct RenderProfileSample {
  qint64 captureNs = 0;
  qint64 staticNs = 0;
  qint64 overlayNs = 0;
  qint64 noteBuildNs = 0;
  qint64 uploadNs = 0;
  qint64 resourceUpdateNs = 0;
  qint64 beginPassNs = 0;
  qint64 passSetupNs = 0;
  qint64 baseDrawNs = 0;
  qint64 noteDrawNs = 0;
  qint64 auraDrawNs = 0;
  qint64 overlayDrawNs = 0;
  qint64 passEndNs = 0;
  qint64 drawNs = 0;
  qint64 totalNs = 0;
  int noteCandidates = 0;
  int horizontallyVisibleNotes = 0;
  int verticallyVisibleNotes = 0;
  int drawnNotes = 0;
  int activeLasers = 0;
  int dynamicInstances = 0;
  int measureLabelInstances = 0;
  int selectionInstances = 0;
  int playheadInstances = 0;
  int marqueeInstances = 0;
  int keyboardInstances = 0;
  int scrollChromeInstances = 0;
};

// Aggregates per-frame timings and prints periodic averages.
void logRenderProfileSample(const RenderProfileSample& sample) {
  static constexpr int kLogEveryFrames = 120;
  struct Totals {
    int frames = 0;
    qint64 captureNs = 0;
    qint64 staticNs = 0;
    qint64 overlayNs = 0;
    qint64 noteBuildNs = 0;
    qint64 uploadNs = 0;
    qint64 resourceUpdateNs = 0;
    qint64 beginPassNs = 0;
    qint64 passSetupNs = 0;
    qint64 baseDrawNs = 0;
    qint64 noteDrawNs = 0;
    qint64 auraDrawNs = 0;
    qint64 overlayDrawNs = 0;
    qint64 passEndNs = 0;
    qint64 drawNs = 0;
    qint64 totalNs = 0;
    int noteCandidates = 0;
    int horizontallyVisibleNotes = 0;
    int verticallyVisibleNotes = 0;
    int drawnNotes = 0;
    int activeLasers = 0;
    int dynamicInstances = 0;
    int measureLabelInstances = 0;
    int selectionInstances = 0;
    int playheadInstances = 0;
    int marqueeInstances = 0;
    int keyboardInstances = 0;
    int scrollChromeInstances = 0;
  };

  static Totals totals;
  ++totals.frames;
  totals.captureNs += sample.captureNs;
  totals.staticNs += sample.staticNs;
  totals.overlayNs += sample.overlayNs;
  totals.noteBuildNs += sample.noteBuildNs;
  totals.uploadNs += sample.uploadNs;
  totals.resourceUpdateNs += sample.resourceUpdateNs;
  totals.beginPassNs += sample.beginPassNs;
  totals.passSetupNs += sample.passSetupNs;
  totals.baseDrawNs += sample.baseDrawNs;
  totals.noteDrawNs += sample.noteDrawNs;
  totals.auraDrawNs += sample.auraDrawNs;
  totals.overlayDrawNs += sample.overlayDrawNs;
  totals.passEndNs += sample.passEndNs;
  totals.drawNs += sample.drawNs;
  totals.totalNs += sample.totalNs;
  totals.noteCandidates += sample.noteCandidates;
  totals.horizontallyVisibleNotes += sample.horizontallyVisibleNotes;
  totals.verticallyVisibleNotes += sample.verticallyVisibleNotes;
  totals.drawnNotes += sample.drawnNotes;
  totals.activeLasers += sample.activeLasers;
  totals.dynamicInstances += sample.dynamicInstances;
  totals.measureLabelInstances += sample.measureLabelInstances;
  totals.selectionInstances += sample.selectionInstances;
  totals.playheadInstances += sample.playheadInstances;
  totals.marqueeInstances += sample.marqueeInstances;
  totals.keyboardInstances += sample.keyboardInstances;
  totals.scrollChromeInstances += sample.scrollChromeInstances;

  if (totals.frames < kLogEveryFrames) {
    return;
  }

  auto avgMs = [&](qint64 ns) -> double {
    return static_cast<double>(ns) / 1000000.0 / static_cast<double>(totals.frames);
  };
  auto avgCount = [&](int count) -> double {
    return static_cast<double>(count) / static_cast<double>(totals.frames);
  };
  qInfo().nospace() << "[PianoRollRhiRenderer] avg over " << totals.frames
                    << " frames: capture=" << avgMs(totals.captureNs) << "ms"
                    << ", static=" << avgMs(totals.staticNs) << "ms"
                    << ", overlays=" << avgMs(totals.overlayNs) << "ms"
                    << ", notes=" << avgMs(totals.noteBuildNs) << "ms"
                    << ", upload=" << avgMs(totals.uploadNs) << "ms"
                    << ", resource-update=" << avgMs(totals.resourceUpdateNs) << "ms"
                    << ", begin-pass=" << avgMs(totals.beginPassNs) << "ms"
                    << ", pass-setup=" << avgMs(totals.passSetupNs) << "ms"
                    << ", base-draw=" << avgMs(totals.baseDrawNs) << "ms"
                    << ", note-draw=" << avgMs(totals.noteDrawNs) << "ms"
                    << ", aura-draw=" << avgMs(totals.auraDrawNs) << "ms"
                    << ", overlay-draw=" << avgMs(totals.overlayDrawNs) << "ms"
                    << ", pass-end=" << avgMs(totals.passEndNs) << "ms"
                    << ", draw=" << avgMs(totals.drawNs) << "ms"
                    << ", total=" << avgMs(totals.totalNs) << "ms"
                    << ", note-candidates=" << avgCount(totals.noteCandidates)
                    << ", note-hvis=" << avgCount(totals.horizontallyVisibleNotes)
                    << ", note-vvis=" << avgCount(totals.verticallyVisibleNotes)
                    << ", note-drawn=" << avgCount(totals.drawnNotes)
                    << ", lasers=" << avgCount(totals.activeLasers)
                    << ", dyn=" << avgCount(totals.dynamicInstances)
                    << " (labels=" << avgCount(totals.measureLabelInstances)
                    << ", sel=" << avgCount(totals.selectionInstances)
                    << ", head=" << avgCount(totals.playheadInstances)
                    << ", marquee=" << avgCount(totals.marqueeInstances)
                    << ", keys=" << avgCount(totals.keyboardInstances)
                    << ", chrome=" << avgCount(totals.scrollChromeInstances) << ")";
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

uint64_t noteIdentityHash(const PianoRollFrame::Note& note) {
  // Stable key for fast active/inactive note matching in overlay rebuilds.
  uint64_t hash = 1469598103934665603ULL;
  auto mix = [&](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<uint64_t>(note.startTick));
  mix(static_cast<uint64_t>(note.duration));
  mix(static_cast<uint64_t>(note.key));
  mix(static_cast<uint64_t>(static_cast<uint32_t>(note.trackIndex)));
  return hash;
}

float noteGlowSeed(const PianoRollFrame::Note& note) {
  return (static_cast<float>(note.startTick) * 0.0079f) +
         (static_cast<float>(note.key) * 0.233f) +
         (static_cast<float>(note.trackIndex + 1) * 0.671f);
}

bool isTrackEnabledForIndex(const std::vector<uint8_t>* trackEnabled, int trackIndex) {
  return !trackEnabled ||
         trackIndex < 0 ||
         trackIndex >= static_cast<int>(trackEnabled->size()) ||
         (*trackEnabled)[static_cast<size_t>(trackIndex)] != 0;
}

QColor colorForTrackIndex(const std::vector<QColor>* trackColors, int trackIndex) {
  if (trackColors && trackIndex >= 0 && trackIndex < static_cast<int>(trackColors->size())) {
    return (*trackColors)[static_cast<size_t>(trackIndex)];
  }
  return QColor::fromHsv((trackIndex * 43) % 360, 190, 235);
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
  m_supportsBaseInstance = m_rhi->isFeatureSupported(QRhi::BaseInstance);

  m_vertexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable,
                                    QRhiBuffer::VertexBuffer,
                                    static_cast<int>(sizeof(kVertices)));
  if (!m_vertexBuffer || !m_vertexBuffer->create()) {
    delete m_vertexBuffer;
    m_vertexBuffer = nullptr;
    return;
  }

  m_indexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable,
                                   QRhiBuffer::IndexBuffer,
                                   static_cast<int>(sizeof(kIndices)));
  if (!m_indexBuffer || !m_indexBuffer->create()) {
    delete m_indexBuffer;
    m_indexBuffer = nullptr;
    return;
  }

  const int ubufSize = m_rhi->ubufAligned(kUniformBytes);
  m_uniformBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, ubufSize);
  if (!m_uniformBuffer || !m_uniformBuffer->create()) {
    delete m_uniformBuffer;
    m_uniformBuffer = nullptr;
    return;
  }

  m_measureLabelSampler = m_rhi->newSampler(QRhiSampler::Linear,
                                            QRhiSampler::Linear,
                                            QRhiSampler::None,
                                            QRhiSampler::ClampToEdge,
                                            QRhiSampler::ClampToEdge);
  if (!m_measureLabelSampler || !m_measureLabelSampler->create()) {
    delete m_measureLabelSampler;
    m_measureLabelSampler = nullptr;
    return;
  }

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

  delete m_activeLaserPipeline;
  m_activeLaserPipeline = nullptr;

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

  delete m_activeLaserInstanceBuffer;
  m_activeLaserInstanceBuffer = nullptr;

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
  m_activeLaserUseScreenBlend = true;
  m_supportsBaseInstance = true;
  m_staticBuffersUploaded = false;
  m_inited = false;
  m_staticBackInstances.clear();
  m_staticFrontInstances.clear();
  m_dynamicInstances.clear();
  m_activeLaserInstances.clear();
  m_noteInstances.clear();
  m_visibleNoteInstances.clear();
  m_dynamicFrontStart = 0;
  m_measureLabelAtlasDirty = true;
  m_measureLabelAtlasScale = 1.0f;
  m_measureLabelHeight = 0.0f;
  m_measureLabelGlyphs = {};
  m_hasStaticCacheKey = false;
  m_staticCacheKey = {};
  m_staticDataDirty = true;
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
  qint64 tOverlays = 0;
  qint64 tNotes = 0;
  qint64 tUpload = 0;
  qint64 tResourceUpdate = 0;
  qint64 tBeginPass = 0;
  qint64 tPassSetup = 0;
  qint64 tBaseDraw = 0;
  qint64 tNoteDraw = 0;
  qint64 tAuraDraw = 0;
  qint64 tOverlayDraw = 0;
  DynamicBuildStats dynamicStats;
  VisibleNoteBuildStats noteStats;
  if (profileEnabled) {
    profileTimer.start();
  }

  const PianoRollFrame::Data frame = m_view->captureRhiFrameData(target.dpr);
  if (frame.viewportSize.isEmpty()) {
    return;
  }

  // Work in logical coordinates; shaders/projector handle device pixels via viewport.
  const QSize logicalSize = frame.viewportSize;

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
  const float currentX = layout.noteAreaLeft + (frame.visualCurrentTick * layout.pixelsPerTick) -
                         layout.scrollX;
  const bool playheadVisible =
      (currentX >= layout.noteAreaLeft - 2.0f && currentX <= layout.noteAreaLeft + layout.noteAreaWidth + 2.0f);

  // Dynamic overlays and visible note state update every frame.
  buildDynamicInstances(frame, layout, currentX, playheadVisible, profileEnabled ? &dynamicStats : nullptr);
  if (profileEnabled) {
    tOverlays = profileTimer.nsecsElapsed();
  }
  buildVisibleNoteInstances(frame, layout, profileEnabled ? &noteStats : nullptr);
  if (profileEnabled) {
    tNotes = profileTimer.nsecsElapsed();
  }

  const float noteBgLuma = (0.2126f * frame.noteBackgroundColor.redF()) +
                           (0.7152f * frame.noteBackgroundColor.greenF()) +
                           (0.0722f * frame.noteBackgroundColor.blueF());
  const bool activeLaserUseScreenBlend = noteBgLuma < 0.58f;
  ensurePipelines(target.renderPassDesc,
                  std::max(1, target.sampleCount),
                  activeLaserUseScreenBlend);
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
  std::array<float, 32> ubo{};
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
  // glowConfig packs glow mode and animation time.
  ubo[28] = activeLaserUseScreenBlend ? 0.0f : 1.0f;
  ubo[29] = frame.elapsedSeconds;
  ubo[30] = 0.0f;
  ubo[31] = 0.0f;
  updates->updateDynamicBuffer(m_uniformBuffer, 0, kUniformBytes, ubo.data());

  if (m_staticDataDirty) {
    const int staticBackBytes = static_cast<int>(m_staticBackInstances.size() * sizeof(RectInstance));
    if (staticBackBytes > 0 && ensureInstanceBuffer(m_staticBackInstanceBuffer, staticBackBytes, 16384)) {
      updates->updateDynamicBuffer(m_staticBackInstanceBuffer,
                                   0,
                                   staticBackBytes,
                                   m_staticBackInstances.data());
    }

    const int staticFrontBytes = static_cast<int>(m_staticFrontInstances.size() * sizeof(RectInstance));
    if (staticFrontBytes > 0 && ensureInstanceBuffer(m_staticFrontInstanceBuffer, staticFrontBytes, 4096)) {
      updates->updateDynamicBuffer(m_staticFrontInstanceBuffer,
                                   0,
                                   staticFrontBytes,
                                   m_staticFrontInstances.data());
    }
    m_staticDataDirty = false;
  }

  auto uploadInstances = [&](QRhiBuffer*& buffer, const auto& instances, int minBytes) {
    const int bytes = static_cast<int>(instances.size() * sizeof(instances[0]));
    if (bytes <= 0 || !ensureInstanceBuffer(buffer, bytes, minBytes)) {
      return;
    }
    updates->updateDynamicBuffer(buffer, 0, bytes, instances.data());
  };

  uploadInstances(m_dynamicInstanceBuffer, m_dynamicInstances, 8192);
  uploadInstances(m_activeLaserInstanceBuffer, m_activeLaserInstances, 4096);
  uploadInstances(m_noteInstanceBuffer, m_visibleNoteInstances, 16384);
  if (profileEnabled) {
    tUpload = profileTimer.nsecsElapsed();
  }

  if (updates) {
    cb->resourceUpdate(updates);
  }
  if (profileEnabled) {
    tResourceUpdate = profileTimer.nsecsElapsed();
  }

  cb->beginPass(target.renderTarget, frame.backgroundColor, {1.0f, 0}, nullptr);
  if (profileEnabled) {
    tBeginPass = profileTimer.nsecsElapsed();
  }
  const QRhiViewport viewport(0,
                              0,
                              static_cast<float>(target.pixelSize.width()),
                              static_cast<float>(target.pixelSize.height()));
  cb->setViewport(viewport);
  if (profileEnabled) {
    tPassSetup = profileTimer.nsecsElapsed();
  }

  auto drawInstances = [&](QRhiGraphicsPipeline* pipeline,
                           QRhiBuffer* buffer,
                           int count,
                           int firstInstance,
                           int instanceStride,
                           bool setStencilRefOne = false) {
    if (!pipeline || !m_shaderBindings || !buffer || count <= 0) {
      return;
    }
    quint32 instanceOffset = 0;
    int drawFirstInstance = firstInstance;
    if (!m_supportsBaseInstance && firstInstance > 0) {
      instanceOffset = static_cast<quint32>(firstInstance * instanceStride);
      drawFirstInstance = 0;
    }
    if (setStencilRefOne) {
      cb->setStencilRef(1);
    }
    cb->setGraphicsPipeline(pipeline);
    cb->setShaderResources(m_shaderBindings);
    const QRhiCommandBuffer::VertexInput bindings[] = {
        {m_vertexBuffer, 0},
        {buffer, instanceOffset},
    };
    cb->setVertexInput(0,
                       static_cast<int>(std::size(bindings)),
                       bindings,
                       m_indexBuffer,
                       0,
                       QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, count, 0, 0, drawFirstInstance);
  };

  const int staticBackCount = static_cast<int>(m_staticBackInstances.size());
  const int staticFrontCount = static_cast<int>(m_staticFrontInstances.size());
  const int activeLaserCount = static_cast<int>(m_activeLaserInstances.size());
  const int visibleNoteCount = static_cast<int>(m_visibleNoteInstances.size());
  drawInstances(m_pipeline,
                m_staticBackInstanceBuffer,
                staticBackCount,
                0,
                static_cast<int>(sizeof(RectInstance)));
  if (profileEnabled) {
    tBaseDraw = profileTimer.nsecsElapsed();
  }

  if (m_noteInstanceBuffer && visibleNoteCount > 0) {
    // Notes write stencil=1 so the aura stays outside every covered pixel.
    drawInstances(m_notePipeline,
                  m_noteInstanceBuffer,
                  visibleNoteCount,
                  0,
                  static_cast<int>(sizeof(NoteInstance)),
                  true);
  }
  if (profileEnabled) {
    tNoteDraw = profileTimer.nsecsElapsed();
  }

  // Render order is intentional:
  // ordered note bodies first, then the outside-only aura, then UI overlays.
  drawInstances(m_activeLaserPipeline,
                m_activeLaserInstanceBuffer,
                activeLaserCount,
                0,
                static_cast<int>(sizeof(RectInstance)),
                  true);
  if (profileEnabled) {
    tAuraDraw = profileTimer.nsecsElapsed();
  }

  const int dynamicCount = static_cast<int>(m_dynamicInstances.size());
  const int dynamicFrontStart = std::clamp(m_dynamicFrontStart, 0, dynamicCount);
  drawInstances(m_pipeline,
                m_dynamicInstanceBuffer,
                dynamicFrontStart,
                0,
                static_cast<int>(sizeof(RectInstance)));
  drawInstances(m_pipeline,
                m_staticFrontInstanceBuffer,
                staticFrontCount,
                0,
                static_cast<int>(sizeof(RectInstance)));
  drawInstances(m_pipeline,
                m_dynamicInstanceBuffer,
                dynamicCount - dynamicFrontStart,
                dynamicFrontStart,
                static_cast<int>(sizeof(RectInstance)));
  if (profileEnabled) {
    tOverlayDraw = profileTimer.nsecsElapsed();
  }

  cb->endPass();

  if (profileEnabled) {
    const qint64 tEnd = profileTimer.nsecsElapsed();
    logRenderProfileSample(RenderProfileSample{
        tCapture,
        tStatic - tCapture,
        tOverlays - tStatic,
        tNotes - tOverlays,
        tUpload - tNotes,
        tResourceUpdate - tUpload,
        tBeginPass - tResourceUpdate,
        tPassSetup - tBeginPass,
        tBaseDraw - tPassSetup,
        tNoteDraw - tBaseDraw,
        tAuraDraw - tNoteDraw,
        tOverlayDraw - tAuraDraw,
        tEnd - tOverlayDraw,
        tEnd - tUpload,
        tEnd,
        noteStats.candidateCount,
        noteStats.horizontallyVisibleCount,
        noteStats.verticallyVisibleCount,
        noteStats.outputCount,
        noteStats.activeLaserCount,
        dynamicStats.totalInstances,
        dynamicStats.measureLabelInstances,
        dynamicStats.selectionInstances,
        dynamicStats.playheadInstances,
        dynamicStats.marqueeInstances,
        dynamicStats.keyboardInstances,
        dynamicStats.scrollChromeInstances,
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
void PianoRollRhiRenderer::ensurePipelines(QRhiRenderPassDescriptor* renderPassDesc,
                                           int sampleCount,
                                           bool activeLaserUseScreenBlend) {
  if (!renderPassDesc) {
    return;
  }

  if (m_pipeline && m_activeLaserPipeline && m_notePipeline && m_shaderBindings &&
      m_outputRenderPass == renderPassDesc && m_sampleCount == sampleCount &&
      m_activeLaserUseScreenBlend == activeLaserUseScreenBlend) {
    return;
  }

  // Render pass/sample count changes require a fresh pipeline.
  auto resetOwned = [](auto*& ptr) {
    delete ptr;
    ptr = nullptr;
  };
  resetOwned(m_notePipeline);
  resetOwned(m_activeLaserPipeline);
  resetOwned(m_pipeline);
  resetOwned(m_shaderBindings);

  m_outputRenderPass = renderPassDesc;
  m_sampleCount = sampleCount;
  m_activeLaserUseScreenBlend = activeLaserUseScreenBlend;

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

  QRhiGraphicsPipeline::TargetBlend activeLaserBlend;
  activeLaserBlend.enable = true;
  if (activeLaserUseScreenBlend) {
    activeLaserBlend.srcColor = QRhiGraphicsPipeline::One;
    activeLaserBlend.dstColor = QRhiGraphicsPipeline::OneMinusSrcColor;
  } else {
    // Light backgrounds need alpha-over composition (not screen) so the glow
    // remains visible without hard clipping to white.
    activeLaserBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    activeLaserBlend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  }
  activeLaserBlend.opColor = QRhiGraphicsPipeline::Add;
  activeLaserBlend.srcAlpha = QRhiGraphicsPipeline::One;
  activeLaserBlend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  activeLaserBlend.opAlpha = QRhiGraphicsPipeline::Add;

  static const QShader rectVertexShader = loadShader(":/shaders/pianorollquad.vert.qsb");
  static const QShader rectFragmentShader = loadShader(":/shaders/pianorollquad.frag.qsb");
  static const QShader noteVertexShader = loadShader(":/shaders/pianorollnote.vert.qsb");
  static const QShader noteFragmentShader = loadShader(":/shaders/pianorollnote.frag.qsb");

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

  auto createPipeline = [&](QRhiGraphicsPipeline*& pipeline,
                            const QRhiVertexInputLayout& inputLayout,
                            const QShader& vs,
                            const QShader& fs,
                            const QRhiGraphicsPipeline::TargetBlend& targetBlend,
                            const QRhiGraphicsPipeline::StencilOpState* stencilFront,
                            const QRhiGraphicsPipeline::StencilOpState* stencilBack,
                            quint8 stencilReadMask,
                            quint8 stencilWriteMask) {
    pipeline = m_rhi->newGraphicsPipeline();
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setSampleCount(m_sampleCount);
    pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });
    pipeline->setShaderResourceBindings(m_shaderBindings);
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setTargetBlends({targetBlend});
    if (stencilFront || stencilBack) {
      pipeline->setStencilTest(true);
      if (stencilFront) {
        pipeline->setStencilFront(*stencilFront);
      }
      if (stencilBack) {
        pipeline->setStencilBack(*stencilBack);
      }
      pipeline->setStencilReadMask(stencilReadMask);
      pipeline->setStencilWriteMask(stencilWriteMask);
    }
    pipeline->setRenderPassDescriptor(renderPassDesc);
    pipeline->create();
  };

  QRhiGraphicsPipeline::StencilOpState activeLaserStencil;
  activeLaserStencil.compareOp = QRhiGraphicsPipeline::NotEqual;
  QRhiVertexInputLayout noteInputLayout;
  noteInputLayout.setBindings({
      {2 * static_cast<int>(sizeof(float))},
      {static_cast<int>(sizeof(NoteInstance)), QRhiVertexInputBinding::PerInstance},
  });
  noteInputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {1, 1, QRhiVertexInputAttribute::Float4, 0},
      {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
      {1, 3, QRhiVertexInputAttribute::Float2, 8 * static_cast<int>(sizeof(float))},
  });

  createPipeline(m_pipeline,
                 rectInputLayout,
                 rectVertexShader,
                 rectFragmentShader,
                 blend,
                 nullptr,
                 nullptr,
                 0x00,
                 0x00);

  // Exclude glow from all note-covered pixels to avoid overlap brightening.
  createPipeline(m_activeLaserPipeline,
                 rectInputLayout,
                 rectVertexShader,
                 rectFragmentShader,
                 activeLaserBlend,
                 &activeLaserStencil,
                 &activeLaserStencil,
                 0xFF,
                 0x00);

  // Mark note pixels in stencil so the active-laser pass can skip them.
  QRhiGraphicsPipeline::StencilOpState noteStencil;
  noteStencil.passOp = QRhiGraphicsPipeline::Replace;
  createPipeline(m_notePipeline,
                 noteInputLayout,
                 noteVertexShader,
                 noteFragmentShader,
                 blend,
                 &noteStencil,
                 &noteStencil,
                 0xFF,
                 0xFF);
}

bool PianoRollRhiRenderer::ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes, int minBytes) {
  if (bytes <= 0) {
    return true;
  }

  const int requiredSize = std::max(bytes, minBytes);
  if (buffer && buffer->size() >= requiredSize) {
    return true;
  }

  int targetSize = requiredSize;
  if (buffer) {
    targetSize = std::max(requiredSize, static_cast<int>(buffer->size()) * 2);
  }

  delete buffer;
  buffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, targetSize);
  if (!buffer || !buffer->create()) {
    delete buffer;
    buffer = nullptr;
    return false;
  }
  return true;
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

  if (!m_measureLabelAtlasDirty) {
    if (!m_measureLabelAtlas) {
      // Keep SRB binding 1 valid even before the first atlas is generated.
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

  if (!m_measureLabelAtlas || m_measureLabelAtlas->pixelSize() != atlas.size()) {
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
  layout.viewWidth = std::max(0, pixelSize.width() - frame.scrollChrome.rightMargin);
  layout.viewHeight = std::max(0, pixelSize.height() - frame.scrollChrome.bottomMargin);
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

void PianoRollRhiRenderer::buildVisibleNoteInstances(const PianoRollFrame::Data& frame,
                                                     const Layout& layout,
                                                     VisibleNoteBuildStats* stats) {
  m_visibleNoteInstances.clear();
  m_activeLaserInstances.clear();
  if (!frame.notes || frame.notes->empty() || m_noteInstances.empty()) {
    return;
  }

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

  const int noteVisibleBeginIndex = std::clamp(static_cast<int>(std::distance(notes.begin(), beginIt)),
                                               0,
                                               static_cast<int>(m_noteInstances.size()));
  const int noteVisibleEndIndex = std::clamp(static_cast<int>(std::distance(notes.begin(), endIt)),
                                             noteVisibleBeginIndex,
                                             static_cast<int>(m_noteInstances.size()));
  if (noteVisibleEndIndex <= noteVisibleBeginIndex) {
    return;
  }
  if (stats) {
    stats->candidateCount = noteVisibleEndIndex - noteVisibleBeginIndex;
  }

  const auto isNoteVerticallyVisible = [&](const PianoRollFrame::Note& note) -> bool {
    const float y = layout.noteAreaTop +
                    ((127.0f - static_cast<float>(note.key)) * layout.pixelsPerKey) -
                    layout.scrollY;
    const float h = std::max(1.0f, layout.pixelsPerKey - 1.0f);
    return (y + h) > layout.noteAreaTop &&
           y < (layout.noteAreaTop + layout.noteAreaHeight);
  };
  const uint64_t visibleEndTickExclusive = layout.visibleEndTick + 1;

  const int visibleCount = noteVisibleEndIndex - noteVisibleBeginIndex;
  m_visibleNoteInstances.reserve(static_cast<size_t>(visibleCount));
  m_enabledVisibleNoteInstances.clear();
  m_enabledVisibleNoteInstances.reserve(static_cast<size_t>(visibleCount));

  std::unordered_map<uint64_t, int> activeCounts;
  const auto* trackEnabled = frame.trackEnabled.get();
  if (frame.activeNotes && !frame.activeNotes->empty()) {
    activeCounts.reserve(frame.activeNotes->size() * 2);
    for (const PianoRollFrame::Note& active : *frame.activeNotes) {
      if (!isTrackEnabledForIndex(trackEnabled, active.trackIndex)) {
        continue;
      }
      ++activeCounts[noteIdentityHash(active)];
    }
  }
  const float inactiveDim = std::clamp(frame.inactiveNoteDimAlpha, 0.0f, 1.0f);
  const float inactiveShade = 1.0f - (0.55f * inactiveDim);

  // Keep muted notes behind enabled notes without rescanning the visible range.
  // We preserve the original order inside each bucket, then append the enabled
  // bucket after the muted one so overlap between the two groups is stable.
  for (int noteIndex = noteVisibleBeginIndex; noteIndex < noteVisibleEndIndex; ++noteIndex) {
    const PianoRollFrame::Note& note = notes[static_cast<size_t>(noteIndex)];
    const uint64_t noteStartTick = static_cast<uint64_t>(note.startTick);
    const uint64_t noteEndTickExclusive =
        noteStartTick + static_cast<uint64_t>(std::max<uint32_t>(1, note.duration));
    const bool horizontallyVisible =
        noteEndTickExclusive > layout.visibleStartTick && noteStartTick < visibleEndTickExclusive;
    if (stats && horizontallyVisible) {
      ++stats->horizontallyVisibleCount;
    }
    if (!isNoteVerticallyVisible(note)) {
      continue;
    }
    if (stats) {
      ++stats->verticallyVisibleCount;
    }

    NoteInstance instance = m_noteInstances[static_cast<size_t>(noteIndex)];
    const bool trackIsEnabled = instance.borderEnabled > 0.5f;

    bool isActive = false;
    if (trackIsEnabled) {
      const auto it = activeCounts.find(noteIdentityHash(note));
      if (it != activeCounts.end() && it->second > 0) {
        --(it->second);
        instance.active = 1.0f;
        isActive = true;
        const NoteGeometry geometry = computeNoteGeometry(note, layout);
        if (geometry.valid) {
          appendActiveLaserForNote(note, geometry, frame.trackColors.get());
          if (stats) {
            ++stats->activeLaserCount;
          }
        }
      }
    }
    if (trackIsEnabled && !isActive && inactiveDim > 0.001f) {
      // Keep active enabled notes readable by darkening only their inactive peers.
      instance.r *= inactiveShade;
      instance.g *= inactiveShade;
      instance.b *= inactiveShade;
    }

    if (trackIsEnabled) {
      m_enabledVisibleNoteInstances.push_back(instance);
    } else {
      m_visibleNoteInstances.push_back(instance);
    }
  }

  if (m_visibleNoteInstances.empty()) {
    m_visibleNoteInstances.swap(m_enabledVisibleNoteInstances);
  } else {
    // Muted notes were pushed directly into m_visibleNoteInstances above, so
    // appending enabled notes here guarantees they draw on top.
    m_visibleNoteInstances.insert(m_visibleNoteInstances.end(),
                                  m_enabledVisibleNoteInstances.begin(),
                                  m_enabledVisibleNoteInstances.end());
  }
  if (stats) {
    stats->outputCount = static_cast<int>(m_visibleNoteInstances.size());
  }
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

  for (const PianoRollFrame::Note& note : *frame.notes) {
    const bool trackIsEnabled = isTrackEnabledForIndex(trackEnabled, note.trackIndex);
    QColor fillColor;
    if (trackIsEnabled) {
      fillColor = colorForTrackIndex(trackColors, note.trackIndex);
      fillColor.setAlpha(188);
    } else {
      const QColor trackColor = colorForTrackIndex(trackColors, note.trackIndex);
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
        trackIsEnabled ? 1.0f : 0.0f,
        fillColor.redF(),
        fillColor.greenF(),
        fillColor.blueF(),
        fillColor.alphaF(),
        0.0f,
        noteGlowSeed(note),
    });
  }
}

// Builds all cached quads that do not depend on playback state.
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

  // Draw the top bar after scrolling lane rows so it stays visually opaque.
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

  appendStaticGridInstances(frame, layout);
  appendStaticKeyboardInstances(frame, layout);

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

// Emits procedural beat/measure grid segments for each time-signature region.
void PianoRollRhiRenderer::appendStaticGridInstances(const PianoRollFrame::Data& frame, const Layout& layout) {
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

    // Beat/measure spacing is packed into instance params and expanded in the fragment shader.
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
      // Signature i owns ticks up to, but not including, the next signature.
      segEnd = std::min<uint64_t>(segEnd, std::max<uint64_t>(segStart, signatures[i + 1].tick));
    }
    appendGridSegment(segStart, segEnd, beatTicks, measureTicks);
  }
}

// Draws static white and black key faces for the keyboard column.
void PianoRollRhiRenderer::appendStaticKeyboardInstances(const PianoRollFrame::Data& frame, const Layout& layout) {
  const KeyboardTopology& topology = keyboardTopology();
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

  // Black key faces are emitted in the static front layer so they stay above white keys.
  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeightUnits = 1.0f;
  const float blackTopInsetLeftPx = 1.0f;
  const float blackTopInsetRightPx = std::clamp(blackKeyWidth * 0.11f, 2.0f, 6.0f);
  const float blackTopInsetYUnits = 0.20f;
  const float blackTopSheenCurve = 9.0f;

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
               innerHUnits,
               blackTop,
               LineStyle::HorizontalGradient,
               0.82f,
               1.16f,
               blackTopSheenCurve,
               0.0f,
               1.0f,
               0.0f,
               1.0f,
               0.0f,
               1.0f);
  }
}

// Builds per-frame overlays and playback-dependent quads.
void PianoRollRhiRenderer::buildDynamicInstances(const PianoRollFrame::Data& frame,
                                                 const Layout& layout,
                                                 float currentX,
                                                 bool playheadVisible,
                                                 DynamicBuildStats* stats) {
  m_dynamicInstances.clear();
  m_dynamicFrontStart = 0;

  if (layout.viewWidth <= 0 || layout.viewHeight <= 0 || layout.noteAreaWidth <= 0.0f || layout.noteAreaHeight <= 0.0f) {
    return;
  }

  const int measureStart = static_cast<int>(m_dynamicInstances.size());
  appendMeasureNumberOverlays(frame, layout);
  if (stats) {
    stats->measureLabelInstances = static_cast<int>(m_dynamicInstances.size()) - measureStart;
  }

  const auto* trackColors = frame.trackColors.get();
  const auto* trackEnabled = frame.trackEnabled.get();

  if (frame.selectedNotes && !frame.selectedNotes->empty()) {
    const int selectionStart = static_cast<int>(m_dynamicInstances.size());
    const float edge = 1.0f;
    const auto appendOutline = [&](float x, float y, float w, float h, const QColor& color) {
      appendRect(m_dynamicInstances, x, y, w, edge, color);
      appendRect(m_dynamicInstances, x, y + h - edge, w, edge, color);
      appendRect(m_dynamicInstances, x, y, edge, h, color);
      appendRect(m_dynamicInstances, x + w - edge, y, edge, h, color);
    };

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
      appendOutline(geometry.x,
                    geometry.y,
                    geometry.w,
                    geometry.h,
                    frame.selectedNoteOutlineColor);
    }
    if (stats) {
      stats->selectionInstances = static_cast<int>(m_dynamicInstances.size()) - selectionStart;
    }
  }

  if (playheadVisible) {
    const int playheadStart = static_cast<int>(m_dynamicInstances.size());
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
    if (stats) {
      stats->playheadInstances = static_cast<int>(m_dynamicInstances.size()) - playheadStart;
    }
  }

  if (frame.selectionRectVisible && frame.selectionRectW > 0.5f && frame.selectionRectH > 0.5f) {
    const int marqueeStart = static_cast<int>(m_dynamicInstances.size());
    const float rawLeft = frame.selectionRectX;
    const float rawTop = frame.selectionRectY;
    const float rawW = frame.selectionRectW;
    const float rawH = frame.selectionRectH;
    const float noteAreaLeft = layout.noteAreaLeft;
    const float noteAreaTop = layout.noteAreaTop;
    const float noteAreaRight = layout.noteAreaLeft + layout.noteAreaWidth;
    const float noteAreaBottom = layout.noteAreaTop + layout.noteAreaHeight;

    // Clip each marquee segment manually so edges vanish correctly when out of view.
    const auto appendClippedToNoteArea = [&](float x, float y, float w, float h, const QColor& color) {
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
    const auto appendClippedOutline = [&](float x, float y, float w, float h, const QColor& color) {
      appendClippedToNoteArea(x, y, w, edge, color);
      appendClippedToNoteArea(x, y + h - edge, w, edge, color);
      appendClippedToNoteArea(x, y, edge, h, color);
      appendClippedToNoteArea(x + w - edge, y, edge, h, color);
    };
    appendClippedOutline(rawLeft,
                         rawTop,
                         rawW,
                         rawH,
                         frame.selectionRectOutlineColor);
    if (stats) {
      stats->marqueeInstances = static_cast<int>(m_dynamicInstances.size()) - marqueeStart;
    }
  }

  const int keyboardStart = static_cast<int>(m_dynamicInstances.size());
  appendKeyboardHighlightInstances(frame, layout, trackColors, trackEnabled);
  if (stats) {
    stats->keyboardInstances = static_cast<int>(m_dynamicInstances.size()) - keyboardStart;
  }
  const int chromeStart = static_cast<int>(m_dynamicInstances.size());
  appendScrollChromeInstances(frame);
  if (stats) {
    stats->scrollChromeInstances = static_cast<int>(m_dynamicInstances.size()) - chromeStart;
    stats->totalInstances = static_cast<int>(m_dynamicInstances.size());
  }
}

// Adds outside-only glow aura quads for currently active notes.
void PianoRollRhiRenderer::appendActiveLaserForNote(const PianoRollFrame::Note& note,
                                                    const NoteGeometry& geometry,
                                                    const std::vector<QColor>* trackColors) {
  QColor laserBase = colorForTrackIndex(trackColors, note.trackIndex).lighter(108);
  laserBase.setAlpha(228);

  appendRect(m_activeLaserInstances,
             geometry.x - kActiveLaserAuraPadPx,
             geometry.y - kActiveLaserAuraPadPx,
             geometry.w + (2.0f * kActiveLaserAuraPadPx),
             geometry.h + (2.0f * kActiveLaserAuraPadPx),
             laserBase,
             LineStyle::ActiveLaser);
}

// Draws keyboard key highlights for currently active notes.
void PianoRollRhiRenderer::appendKeyboardHighlightInstances(
    const PianoRollFrame::Data& frame,
    const Layout& layout,
    const std::vector<QColor>* trackColors,
    const std::vector<uint8_t>* trackEnabled) {
  const float blackKeyWidth = std::max(10.0f, layout.keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, layout.pixelsPerKey);
  const KeyboardTopology& topology = keyboardTopology();
  for (int key : topology.whiteKeys) {
    const KeyboardKeyTopology& keyTopology = topology.keys[static_cast<size_t>(key)];
    const float keyTop = layout.noteAreaTop + (keyTopology.topSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float keyBottom =
        layout.noteAreaTop + (keyTopology.bottomSeamUnit * layout.pixelsPerKey) - layout.scrollY;
    const float clippedTop = std::max(layout.noteAreaTop, keyTop);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, keyBottom);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0 || !isTrackEnabledForIndex(trackEnabled, activeTrack)) {
      continue;
    }

    QColor active = colorForTrackIndex(trackColors, activeTrack).lighter(112);
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
    const float centerY =
        layout.noteAreaTop + (topology.keys[static_cast<size_t>(key)].centerUnit * layout.pixelsPerKey) -
        layout.scrollY;
    const float blackY = centerY - (blackKeyHeight * 0.5f);
    const float clippedTop = std::max(layout.noteAreaTop, blackY);
    const float clippedBottom = std::min(layout.noteAreaTop + layout.noteAreaHeight, blackY + blackKeyHeight);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack < 0 || !isTrackEnabledForIndex(trackEnabled, activeTrack)) {
      continue;
    }

    QColor active = colorForTrackIndex(trackColors, activeTrack).lighter(133);
    active.setAlpha(238);
    appendRect(m_dynamicInstances,
               1.0f,
               clippedTop + 1.0f,
               std::max(0.0f, blackKeyWidth - 2.0f),
               std::max(0.0f, clippedBottom - clippedTop - 2.0f),
               active);
  }
}

void PianoRollRhiRenderer::appendScrollChromeInstances(const PianoRollFrame::Data& frame) {
  const RhiScrollAreaChromeSnapshot& chrome = frame.scrollChrome;
  if (!chrome.horizontal.visible && !chrome.vertical.visible &&
      chrome.horizontalButtons.empty() && chrome.verticalButtons.empty()) {
    return;
  }

  const auto appendOutline = [&](const QRectF& rect) {
    if (rect.width() <= 0.5f || rect.height() <= 0.5f) {
      return;
    }
    const float edge = 1.0f;
    appendRect(m_dynamicInstances, rect.left(), rect.top(), rect.width(), edge, chrome.colors.borderColor);
    appendRect(m_dynamicInstances, rect.left(), rect.bottom() - edge, rect.width(), edge, chrome.colors.borderColor);
    appendRect(m_dynamicInstances, rect.left(), rect.top(), edge, rect.height(), chrome.colors.borderColor);
    appendRect(m_dynamicInstances, rect.right() - edge, rect.top(), edge, rect.height(), chrome.colors.borderColor);
  };

  const auto appendButtonGlyph = [&](const RhiScrollButtonSnapshot& button, Qt::Orientation orientation) {
    if (!button.visible || button.rect.width() <= 0.5f || button.rect.height() <= 0.5f) {
      return;
    }

    const float inset = std::max(
        3.0f,
        static_cast<float>(std::min(button.rect.width(), button.rect.height()) * 0.22));
    const QRectF glyphRect = button.rect.adjusted(inset, inset, -inset, -inset);
    switch (button.glyph) {
      case RhiScrollButtonGlyph::Minus: {
        const float thickness = 1.0f;
        appendRect(m_dynamicInstances,
                   glyphRect.left(),
                   glyphRect.center().y() - (thickness * 0.5f),
                   glyphRect.width(),
                   thickness,
                   chrome.colors.glyphColor);
        break;
      }
      case RhiScrollButtonGlyph::Plus: {
        const float thickness = 1.0f;
        appendRect(m_dynamicInstances,
                   glyphRect.left(),
                   glyphRect.center().y() - (thickness * 0.5f),
                   glyphRect.width(),
                   thickness,
                   chrome.colors.glyphColor);
        appendRect(m_dynamicInstances,
                   glyphRect.center().x() - (thickness * 0.5f),
                   glyphRect.top(),
                   thickness,
                   glyphRect.height(),
                   chrome.colors.glyphColor);
        break;
      }
      case RhiScrollButtonGlyph::Backward: {
        const LineStyle style = (orientation == Qt::Horizontal) ? LineStyle::TriangleLeft : LineStyle::TriangleUp;
        appendRect(m_dynamicInstances,
                   glyphRect.left(),
                   glyphRect.top(),
                   glyphRect.width(),
                   glyphRect.height(),
                   chrome.colors.glyphColor,
                   style,
                   1.0f,
                   1.0f);
        break;
      }
      case RhiScrollButtonGlyph::Forward: {
        const LineStyle style = (orientation == Qt::Horizontal) ? LineStyle::TriangleRight : LineStyle::TriangleDown;
        appendRect(m_dynamicInstances,
                   glyphRect.left(),
                   glyphRect.top(),
                   glyphRect.width(),
                   glyphRect.height(),
                   chrome.colors.glyphColor,
                   style,
                   1.0f,
                   1.0f);
        break;
      }
    }
  };

  const auto buttonFillColor = [&](const RhiScrollButtonSnapshot& button) {
    return button.pressed
        ? chrome.colors.buttonPressedColor
        : (button.hovered ? chrome.colors.buttonHoverColor : chrome.colors.laneColor);
  };

  const auto appendButton = [&](const RhiScrollButtonSnapshot& button, Qt::Orientation orientation) {
    if (!button.visible) {
      return;
    }
    appendRect(m_dynamicInstances,
               button.rect.left(),
               button.rect.top(),
               button.rect.width(),
               button.rect.height(),
               buttonFillColor(button));
    appendOutline(button.rect);
    appendButtonGlyph(button, orientation);
  };

  const auto appendBar = [&](const RhiScrollBarSnapshot& bar, const std::vector<RhiScrollButtonSnapshot>& buttons) {
    if (!bar.visible || bar.laneRect.width() <= 0.5f || bar.laneRect.height() <= 0.5f) {
      return;
    }

    appendRect(m_dynamicInstances,
               bar.laneRect.left(),
               bar.laneRect.top(),
               bar.laneRect.width(),
               bar.laneRect.height(),
               chrome.colors.laneColor);
    appendOutline(bar.laneRect);

    if (bar.thumbRect.width() > 0.5f && bar.thumbRect.height() > 0.5f) {
      const QColor thumbColor = bar.thumbPressed
          ? chrome.colors.thumbPressedColor
          : (bar.thumbHovered ? chrome.colors.thumbHoverColor : chrome.colors.thumbColor);
      appendRect(m_dynamicInstances,
                 bar.thumbRect.left(),
                 bar.thumbRect.top(),
                 bar.thumbRect.width(),
                 bar.thumbRect.height(),
                 thumbColor,
                 LineStyle::ScrollThumb);
    }

    for (const RhiScrollButtonSnapshot& button : bar.arrowButtons) {
      appendButton(button, bar.orientation);
    }

    for (const RhiScrollButtonSnapshot& button : buttons) {
      appendButton(button, bar.orientation);
    }
  };

  appendBar(chrome.horizontal, chrome.horizontalButtons);
  appendBar(chrome.vertical, chrome.verticalButtons);

  if (chrome.cornerRect.width() > 0.5f && chrome.cornerRect.height() > 0.5f) {
    appendRect(m_dynamicInstances,
               chrome.cornerRect.left(),
               chrome.cornerRect.top(),
               chrome.cornerRect.width(),
               chrome.cornerRect.height(),
               chrome.colors.laneColor);
    appendOutline(chrome.cornerRect);
  }
}

// Computes a note quad in screen space and clips it to the note area.
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
