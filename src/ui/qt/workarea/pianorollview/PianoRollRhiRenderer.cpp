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

  const int ubufSize = m_rhi->ubufAligned(kMat4Bytes);
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

  delete m_instanceBuffer;
  m_instanceBuffer = nullptr;

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

  buildInstances(frame, logicalSize);
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
  updates->updateDynamicBuffer(m_uniformBuffer, 0, kMat4Bytes, mvp.constData());

  if (!m_instances.empty()) {
    const int instanceBytes = static_cast<int>(m_instances.size() * sizeof(RectInstance));
    if (ensureInstanceBuffer(instanceBytes)) {
      updates->updateDynamicBuffer(m_instanceBuffer, 0, instanceBytes, m_instances.data());
    }
  }

  cb->beginPass(target.renderTarget, frame.backgroundColor, {1.0f, 0}, updates);
  cb->setViewport(QRhiViewport(0,
                               0,
                               static_cast<float>(target.pixelSize.width()),
                               static_cast<float>(target.pixelSize.height())));

  if (m_pipeline && m_shaderBindings && !m_instances.empty() && m_instanceBuffer) {
    cb->setGraphicsPipeline(m_pipeline);
    cb->setShaderResources(m_shaderBindings);

    const QRhiCommandBuffer::VertexInput bindings[] = {
        {m_vertexBuffer, 0},
        {m_instanceBuffer, 0},
    };
    cb->setVertexInput(0,
                       static_cast<int>(std::size(bindings)),
                       bindings,
                       m_indexBuffer,
                       0,
                       QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6, static_cast<int>(m_instances.size()), 0, 0, 0);
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

void PianoRollRhiRenderer::ensurePipeline(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount) {
  if (!renderPassDesc) {
    return;
  }

  if (m_pipeline && m_shaderBindings && m_outputRenderPass == renderPassDesc && m_sampleCount == sampleCount) {
    return;
  }

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

  QShader vertexShader = loadShader(":/shaders/hexquad.vert.qsb");
  QShader fragmentShader = loadShader(":/shaders/hexquad.frag.qsb");

  QRhiVertexInputLayout inputLayout;
  inputLayout.setBindings({
      {2 * static_cast<int>(sizeof(float))},
      {static_cast<int>(sizeof(RectInstance)), QRhiVertexInputBinding::PerInstance},
  });
  inputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {1, 1, QRhiVertexInputAttribute::Float4, 0},
      {1, 2, QRhiVertexInputAttribute::Float4, 4 * static_cast<int>(sizeof(float))},
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

bool PianoRollRhiRenderer::ensureInstanceBuffer(int bytes) {
  if (bytes <= 0) {
    return true;
  }

  const int targetSize = std::max(bytes, 8192);
  if (m_instanceBuffer && m_instanceBuffer->size() >= targetSize) {
    return true;
  }

  delete m_instanceBuffer;
  m_instanceBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, targetSize);
  return m_instanceBuffer->create();
}

void PianoRollRhiRenderer::buildInstances(const PianoRollFrame::Data& frame, const QSize& pixelSize) {
  m_instances.clear();

  const int viewWidth = pixelSize.width();
  const int viewHeight = pixelSize.height();
  if (viewWidth <= 0 || viewHeight <= 0) {
    return;
  }

  const float keyboardWidth = std::clamp(static_cast<float>(frame.keyboardWidth),
                                         36.0f,
                                         static_cast<float>(viewWidth));
  const float topBarHeight = std::clamp(static_cast<float>(frame.topBarHeight),
                                        14.0f,
                                        static_cast<float>(viewHeight));

  const float noteAreaLeft = keyboardWidth;
  const float noteAreaTop = topBarHeight;
  const float noteAreaWidth = std::max(0.0f, static_cast<float>(viewWidth) - noteAreaLeft);
  const float noteAreaHeight = std::max(0.0f, static_cast<float>(viewHeight) - noteAreaTop);

  appendRect(0.0f, 0.0f, static_cast<float>(viewWidth), static_cast<float>(viewHeight), frame.backgroundColor);
  appendRect(0.0f, 0.0f, keyboardWidth, topBarHeight, frame.keyboardBackgroundColor.darker(108));

  if (noteAreaWidth <= 0.0f || noteAreaHeight <= 0.0f) {
    appendRect(keyboardWidth - 1.0f, 0.0f, 1.0f, static_cast<float>(viewHeight), frame.dividerColor);
    return;
  }

  appendRect(noteAreaLeft, 0.0f, noteAreaWidth, topBarHeight, frame.topBarBackgroundColor);
  appendRect(noteAreaLeft, noteAreaTop, noteAreaWidth, noteAreaHeight, frame.noteBackgroundColor);
  appendRect(0.0f, noteAreaTop, keyboardWidth, noteAreaHeight, frame.keyboardBackgroundColor);

  const float pixelsPerTick = std::max(0.0001f, frame.pixelsPerTick);
  const float pixelsPerKey = std::max(1.0f, frame.pixelsPerKey);
  const float scrollX = static_cast<float>(std::max(0, frame.scrollX));
  const float scrollY = static_cast<float>(std::max(0, frame.scrollY));

  const float visibleStartTickF = scrollX / pixelsPerTick;
  const float visibleEndTickF = (scrollX + noteAreaWidth) / pixelsPerTick;
  const uint64_t visibleStartTick = static_cast<uint64_t>(std::max(0.0f, std::floor(visibleStartTickF)));
  const uint64_t visibleEndTick = static_cast<uint64_t>(std::max(0.0f, std::ceil(visibleEndTickF)));

  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    const float keyY = noteAreaTop + ((127.0f - static_cast<float>(key)) * pixelsPerKey) - scrollY;
    const float keyH = std::max(1.0f, pixelsPerKey);
    if (keyY >= static_cast<float>(viewHeight) || keyY + keyH <= noteAreaTop) {
      continue;
    }

    const QColor rowColor = isBlackKey(key) ? frame.blackKeyRowColor : frame.whiteKeyRowColor;
    appendRect(noteAreaLeft, keyY, noteAreaWidth, keyH, rowColor);

    QColor rowSep = frame.keySeparatorColor;
    rowSep.setAlpha(std::max(24, rowSep.alpha()));
    appendRect(noteAreaLeft, keyY, noteAreaWidth, 1.0f, rowSep);
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
    uint64_t segEnd = visibleEndTick + measureTicks + 1;
    if (i + 1 < signatures.size()) {
      segEnd = std::max<uint64_t>(segStart, signatures[i + 1].tick);
    }

    if (segEnd <= visibleStartTick || segStart > visibleEndTick) {
      continue;
    }

    uint64_t firstBeat = segStart;
    if (visibleStartTick > segStart) {
      const uint64_t delta = visibleStartTick - segStart;
      firstBeat = segStart + ((delta + beatTicks - 1) / beatTicks) * beatTicks;
    }

    for (uint64_t tick = firstBeat; tick <= visibleEndTick && tick < segEnd; tick += beatTicks) {
      const float x = noteAreaLeft + (static_cast<float>(tick) * pixelsPerTick) - scrollX;
      if (x < noteAreaLeft - 1.0f || x > noteAreaLeft + noteAreaWidth + 1.0f) {
        continue;
      }

      const bool isMeasure = (measureTicks > 0) && (((tick - segStart) % measureTicks) == 0);
      if (isMeasure) {
        appendRect(x, noteAreaTop, 1.0f, noteAreaHeight, frame.measureLineColor);
        QColor topMeasure = frame.measureLineColor;
        topMeasure.setAlpha(std::min(255, topMeasure.alpha() + 30));
        appendRect(x, 0.0f, 1.0f, topBarHeight, topMeasure);
      } else {
        appendDottedLine(x, noteAreaTop, noteAreaHeight, 4.0f, 4.0f, frame.beatLineColor);
        QColor topBeat = frame.beatLineColor;
        topBeat.setAlpha(std::max(20, topBeat.alpha() / 2));
        appendRect(x, 0.0f, 1.0f, topBarHeight, topBeat);
      }

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

  if (frame.notes && !frame.notes->empty()) {
    for (const auto& note : *frame.notes) {
      if (note.startTick > visibleEndTick) {
        break;
      }

      const uint64_t noteDuration = std::max<uint64_t>(1, note.duration);
      const uint64_t noteEndTick = static_cast<uint64_t>(note.startTick) + noteDuration;
      if (noteEndTick < visibleStartTick) {
        continue;
      }

      if (note.key >= PianoRollFrame::kMidiKeyCount) {
        continue;
      }

      const float x = noteAreaLeft + (static_cast<float>(note.startTick) * pixelsPerTick) - scrollX;
      const float x2 = noteAreaLeft + (static_cast<float>(noteEndTick) * pixelsPerTick) - scrollX;
      const float y = noteAreaTop + ((127.0f - static_cast<float>(note.key)) * pixelsPerKey) - scrollY;
      const float h = std::max(1.0f, pixelsPerKey - 1.0f);

      const float clippedX = std::max(noteAreaLeft, x);
      const float clippedX2 = std::min(noteAreaLeft + noteAreaWidth, x2);
      const float clippedY = std::max(noteAreaTop, y);
      const float clippedY2 = std::min(noteAreaTop + noteAreaHeight, y + h);

      const float w = clippedX2 - clippedX;
      const float hh = clippedY2 - clippedY;
      if (w <= 0.5f || hh <= 0.5f) {
        continue;
      }

      QColor noteColor = colorForTrack(note.trackIndex);
      const bool active = frame.currentTick >= static_cast<int>(note.startTick) &&
                          frame.currentTick < static_cast<int>(noteEndTick);

      if (active) {
        noteColor = noteColor.lighter(148);
        noteColor.setAlpha(245);
      } else {
        noteColor.setAlpha(188);
      }

      appendRect(clippedX, clippedY, w, hh, noteColor);

      if (active) {
        const float basePhase = frame.elapsedSeconds * 2.5f + static_cast<float>(note.startTick) * 0.009f;
        for (int ring = 0; ring < 3; ++ring) {
          const float ringPhase = std::fmod(basePhase + (static_cast<float>(ring) * 0.33f), 1.0f);
          const float expand = (1.5f + (ringPhase * 13.0f) + (ring * 1.5f));
          const float intensity = (1.0f - ringPhase) * (0.62f - (ring * 0.14f));
          if (intensity <= 0.0f) {
            continue;
          }

          const float gx = std::max(noteAreaLeft, clippedX - expand);
          const float gy = std::max(noteAreaTop, clippedY - expand);
          const float gx2 = std::min(noteAreaLeft + noteAreaWidth, clippedX + w + expand);
          const float gy2 = std::min(noteAreaTop + noteAreaHeight, clippedY + hh + expand);
          if (gx2 - gx <= 0.5f || gy2 - gy <= 0.5f) {
            continue;
          }

          QColor glow = noteColor;
          glow.setAlpha(std::clamp(static_cast<int>(std::lround(255.0f * intensity)), 0, 255));
          appendRect(gx, gy, gx2 - gx, gy2 - gy, glow);
        }
      }

      QColor noteEdge = frame.noteOutlineColor;
      if (active) {
        noteEdge.setAlpha(std::min(255, noteEdge.alpha() + 95));
      }
      const float edgeThickness = 1.0f;
      appendRect(clippedX, clippedY, w, edgeThickness, noteEdge);
      appendRect(clippedX, clippedY + hh - edgeThickness, w, edgeThickness, noteEdge);
    }
  }

  const float currentX = noteAreaLeft + (static_cast<float>(frame.currentTick) * pixelsPerTick) - scrollX;
  if (currentX >= noteAreaLeft - 2.0f && currentX <= noteAreaLeft + noteAreaWidth + 2.0f) {
    QColor scanColor = frame.scanLineColor;
    if (!frame.playbackActive) {
      scanColor.setAlpha(std::max(100, scanColor.alpha() / 2));
    }

    appendRect(currentX - 1.0f, noteAreaTop, 2.0f, noteAreaHeight, scanColor);
    appendRect(currentX - 1.0f, 0.0f, 2.0f, topBarHeight, scanColor);
    appendRect(currentX - 4.0f, 0.0f, 8.0f, 3.0f, scanColor);

    QColor progress = frame.topBarProgressColor;
    progress.setAlpha(std::min(255, progress.alpha() + 45));
    const float progressWidth = std::clamp(currentX - noteAreaLeft, 0.0f, noteAreaWidth);
    if (progressWidth > 0.5f) {
      appendRect(noteAreaLeft, 0.0f, progressWidth, topBarHeight, progress);
    }
  }

  const float blackKeyWidth = std::max(10.0f, keyboardWidth * 0.63f);
  const float blackKeyHeight = std::max(2.0f, pixelsPerKey);
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

    const float seamY = noteAreaTop + (seamUnit * pixelsPerKey) - scrollY;
    if (seamY < noteAreaTop - 1.0f || seamY > noteAreaTop + noteAreaHeight + 1.0f) {
      return;
    }

    QColor keySep = frame.keySeparatorColor;
    keySep.setAlpha(std::max(56, keySep.alpha()));
    appendRect(0.0f, seamY, keyboardWidth, 1.0f, keySep);
  };

  // White keys are drawn from seam-to-seam so black-key seams align to black-note centers.
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

    const float keyTop = noteAreaTop + (topSeamUnit * pixelsPerKey) - scrollY;
    const float keyBottom = noteAreaTop + (bottomSeamUnit * pixelsPerKey) - scrollY;
    const float clippedTop = std::max(noteAreaTop, keyTop);
    const float clippedBottom = std::min(noteAreaTop + noteAreaHeight, keyBottom);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    appendRect(0.0f, clippedTop, keyboardWidth, clippedBottom - clippedTop, frame.whiteKeyColor);

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack >= 0) {
      QColor active = colorForTrack(activeTrack).lighter(112);
      active.setAlpha(172);
      appendRect(1.0f,
                 clippedTop + 1.0f,
                 std::max(0.0f, keyboardWidth - 2.0f),
                 std::max(0.0f, clippedBottom - clippedTop - 2.0f),
                 active);
    }

    drawSeamAtUnit(topSeamUnit);
    drawSeamAtUnit(bottomSeamUnit);
  }

  // Black keys are centered on black-note rows and overlay the shorter key section.
  for (int key = 0; key < PianoRollFrame::kMidiKeyCount; ++key) {
    if (!isBlackKey(key)) {
      continue;
    }

    const float centerY = noteAreaTop + (centerUnitForKey(key) * pixelsPerKey) - scrollY;
    const float blackY = centerY - (blackKeyHeight * 0.5f);
    const float clippedTop = std::max(noteAreaTop, blackY);
    const float clippedBottom = std::min(noteAreaTop + noteAreaHeight, blackY + blackKeyHeight);
    if (clippedBottom - clippedTop <= 0.5f) {
      continue;
    }

    appendRect(0.0f, clippedTop, blackKeyWidth, clippedBottom - clippedTop, frame.blackKeyColor);

    QColor blackFace = frame.blackKeyColor.darker(116);
    blackFace.setAlpha(230);
    const float innerW = blackKeyWidth * blackInnerWidthRatio;
    const float innerH = (clippedBottom - clippedTop) * blackInnerHeightRatio;
    const float innerX = (blackKeyWidth - innerW) * 0.5f;
    const float innerY = clippedTop + 0.6f;
    appendRect(innerX, innerY, innerW, std::max(0.0f, innerH - 0.6f), blackFace);

    QColor blackHighlight = frame.blackKeyColor.lighter(128);
    blackHighlight.setAlpha(84);
    appendRect(0.0f, clippedTop, blackKeyWidth, 1.0f, blackHighlight);

    const int activeTrack = frame.activeKeyTrack[static_cast<size_t>(key)];
    if (activeTrack >= 0) {
      QColor active = colorForTrack(activeTrack).lighter(133);
      active.setAlpha(238);
      appendRect(1.0f,
                 clippedTop + 1.0f,
                 std::max(0.0f, blackKeyWidth - 2.0f),
                 std::max(0.0f, clippedBottom - clippedTop - 2.0f),
                 active);
    }
  }

  appendRect(noteAreaLeft, topBarHeight - 1.0f, noteAreaWidth, 1.0f, frame.dividerColor);
  appendRect(noteAreaLeft - 1.0f, 0.0f, 1.0f, static_cast<float>(viewHeight), frame.dividerColor);
}

void PianoRollRhiRenderer::appendRect(float x, float y, float w, float h, const QColor& color) {
  if (w <= 0.0f || h <= 0.0f || color.alpha() <= 0) {
    return;
  }

  m_instances.push_back(RectInstance{
      x,
      y,
      w,
      h,
      color.redF(),
      color.greenF(),
      color.blueF(),
      color.alphaF(),
  });
}

void PianoRollRhiRenderer::appendDottedLine(float x,
                                            float y,
                                            float h,
                                            float segmentHeight,
                                            float gapHeight,
                                            const QColor& color) {
  if (h <= 0.0f || segmentHeight <= 0.0f || color.alpha() <= 0) {
    return;
  }

  const float step = std::max(1.0f, segmentHeight + std::max(0.0f, gapHeight));
  const float endY = y + h;
  for (float yy = y; yy < endY; yy += step) {
    const float segH = std::min(segmentHeight, endY - yy);
    appendRect(x, yy, 1.0f, segH, color);
  }
}
