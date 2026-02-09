/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteRhiRenderer.h"

#include "ActiveNoteView.h"

#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <QColor>
#include <QFile>
#include <QMatrix4x4>

#include <array>
#include <algorithm>
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

ActiveNoteRhiRenderer::ActiveNoteRhiRenderer(ActiveNoteView* view)
    : m_view(view) {
}

ActiveNoteRhiRenderer::~ActiveNoteRhiRenderer() {
  releaseResources();
}

void ActiveNoteRhiRenderer::initIfNeeded(QRhi* rhi) {
  if (m_inited || !rhi) {
    return;
  }

  m_rhi = rhi;

  m_vertexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                    static_cast<int>(sizeof(kVertices)));
  m_vertexBuffer->create();

  m_indexBuffer = m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                   static_cast<int>(sizeof(kIndices)));
  m_indexBuffer->create();

  const int ubufSize = m_rhi->ubufAligned(kMat4Bytes);
  m_uniformBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, ubufSize);
  m_uniformBuffer->create();

  m_staticBuffersUploaded = false;
  m_inited = true;
}

void ActiveNoteRhiRenderer::releaseResources() {
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

void ActiveNoteRhiRenderer::renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target) {
  if (!m_inited) {
    initIfNeeded(m_rhi);
  }

  if (!cb || !m_rhi || !m_view || !target.renderTarget || !target.renderPassDesc || target.pixelSize.isEmpty()) {
    return;
  }

  const ActiveNoteFrame::Data frame = m_view->captureRhiFrameData(target.dpr);
  if (frame.viewportSize.isEmpty()) {
    return;
  }

  buildInstances(frame);
  ensurePipeline(target.renderPassDesc, std::max(1, target.sampleCount));

  QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
  if (!m_staticBuffersUploaded) {
    updates->uploadStaticBuffer(m_vertexBuffer, kVertices);
    updates->uploadStaticBuffer(m_indexBuffer, kIndices);
    m_staticBuffersUploaded = true;
  }

  QMatrix4x4 mvp;
  mvp.ortho(0.0f,
            static_cast<float>(target.pixelSize.width()),
            static_cast<float>(target.pixelSize.height()),
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
  cb->setViewport(QRhiViewport(0, 0,
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

bool ActiveNoteRhiRenderer::isBlackKey(int key) {
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

void ActiveNoteRhiRenderer::ensurePipeline(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount) {
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

bool ActiveNoteRhiRenderer::ensureInstanceBuffer(int bytes) {
  if (bytes <= 0) {
    return true;
  }

  const int targetSize = std::max(bytes, 4096);
  if (m_instanceBuffer && m_instanceBuffer->size() >= targetSize) {
    return true;
  }

  delete m_instanceBuffer;
  m_instanceBuffer = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, targetSize);
  return m_instanceBuffer->create();
}

void ActiveNoteRhiRenderer::buildInstances(const ActiveNoteFrame::Data& frame) {
  m_instances.clear();

  const int viewWidth = frame.viewportSize.width();
  const int viewHeight = frame.viewportSize.height();
  if (viewWidth <= 0 || viewHeight <= 0 || frame.trackCount <= 0) {
    return;
  }

  struct KeyGeometry {
    float x = 0.0f;
    float width = 0.0f;
    bool valid = false;
  };

  std::array<KeyGeometry, 128> keyGeometry{};
  int whiteCount = 0;
  for (int key = 0; key < 128; ++key) {
    if (!isBlackKey(key)) {
      ++whiteCount;
    }
  }
  if (whiteCount <= 0) {
    return;
  }

  const float keyboardWidth = static_cast<float>(viewWidth);
  const float whiteWidth = keyboardWidth / static_cast<float>(whiteCount);

  int whiteIndex = 0;
  for (int key = 0; key < 128; ++key) {
    if (!isBlackKey(key)) {
      keyGeometry[static_cast<size_t>(key)] = KeyGeometry{
          static_cast<float>(whiteIndex) * whiteWidth,
          whiteWidth,
          true,
      };
      ++whiteIndex;
    }
  }

  const float blackWidth = std::max(2.0f, whiteWidth * 0.62f);
  for (int key = 0; key < 128; ++key) {
    if (!isBlackKey(key)) {
      continue;
    }

    int prevWhite = key - 1;
    while (prevWhite >= 0 && isBlackKey(prevWhite)) {
      --prevWhite;
    }
    int nextWhite = key + 1;
    while (nextWhite < 128 && isBlackKey(nextWhite)) {
      ++nextWhite;
    }
    if (prevWhite < 0 || nextWhite >= 128) {
      continue;
    }
    if (!keyGeometry[static_cast<size_t>(prevWhite)].valid ||
        !keyGeometry[static_cast<size_t>(nextWhite)].valid) {
      continue;
    }

    const float splitX =
        keyGeometry[static_cast<size_t>(prevWhite)].x +
        keyGeometry[static_cast<size_t>(prevWhite)].width;
    float x = splitX - (blackWidth * 0.5f);
    x = std::clamp(x, 0.0f, keyboardWidth - blackWidth);
    keyGeometry[static_cast<size_t>(key)] = KeyGeometry{x, blackWidth, true};
  }

  const float rowGap = (frame.trackCount > 1)
      ? std::max(1.0f, std::min(4.0f, static_cast<float>(viewHeight) * 0.015f))
      : 0.0f;
  const float totalGaps = rowGap * static_cast<float>(frame.trackCount - 1);
  const float keyboardHeight =
      (static_cast<float>(viewHeight) - totalGaps) / static_cast<float>(frame.trackCount);
  if (keyboardHeight <= 1.0f) {
    return;
  }

  const float blackHeight = std::max(2.0f, keyboardHeight * 0.62f);
  const float whiteTopHeight = std::min(keyboardHeight - 1.0f, blackHeight);
  const float notchWidth = std::min(whiteWidth * 0.35f, std::max(1.0f, whiteWidth * 0.22f));

  for (int track = 0; track < frame.trackCount; ++track) {
    const float y = static_cast<float>(track) * (keyboardHeight + rowGap);

    QColor trackColor = (track < static_cast<int>(frame.trackColors.size()))
        ? frame.trackColors[static_cast<size_t>(track)]
        : QColor::fromHsv((track * 43) % 360, 190, 235);

    QColor rowBackground = trackColor;
    rowBackground.setAlpha(26);
    appendRect(0.0f, y, keyboardWidth, keyboardHeight, rowBackground);

    for (int key = 0; key < 128; ++key) {
      if (isBlackKey(key)) {
        continue;
      }
      const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
      if (!geom.valid) {
        continue;
      }

      const bool hasLeftBlack = key > 0 && isBlackKey(key - 1);
      const bool hasRightBlack = key < 127 && isBlackKey(key + 1);

      QColor whiteKeyColor = frame.whiteKeyColor;
      whiteKeyColor.setAlpha(255);
      appendWhiteKeyShape(geom.x,
                          y,
                          geom.width,
                          keyboardHeight,
                          whiteTopHeight,
                          hasLeftBlack,
                          hasRightBlack,
                          notchWidth,
                          whiteKeyColor);
    }

    if (frame.playbackActive && track < static_cast<int>(frame.activeKeysByTrack.size())) {
      const auto& activeMask = frame.activeKeysByTrack[static_cast<size_t>(track)];
      for (int key = 0; key < 128; ++key) {
        if (isBlackKey(key)) {
          continue;
        }
        if (!activeMask.test(static_cast<size_t>(key))) {
          continue;
        }
        const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
        if (!geom.valid) {
          continue;
        }

        const bool hasLeftBlack = key > 0 && isBlackKey(key - 1);
        const bool hasRightBlack = key < 127 && isBlackKey(key + 1);
        QColor activeWhite = trackColor;
        activeWhite.setAlpha(150);
        appendWhiteKeyShape(geom.x,
                            y,
                            geom.width,
                            keyboardHeight,
                            whiteTopHeight,
                            hasLeftBlack,
                            hasRightBlack,
                            notchWidth,
                            activeWhite);
      }
    }

    QColor keyEdge = frame.separatorColor;
    keyEdge.setAlpha(170);
    for (int key = 0; key < 128; ++key) {
      if (isBlackKey(key)) {
        continue;
      }
      const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
      if (!geom.valid) {
        continue;
      }
      appendRect(geom.x, y, 1.0f, keyboardHeight, keyEdge);
    }
    appendRect(keyboardWidth - 1.0f, y, 1.0f, keyboardHeight, keyEdge);
    appendRect(0.0f, y + keyboardHeight - 1.0f, keyboardWidth, 1.0f, keyEdge);

    for (int key = 0; key < 128; ++key) {
      if (!isBlackKey(key)) {
        continue;
      }

      const KeyGeometry& geom = keyGeometry[static_cast<size_t>(key)];
      if (!geom.valid) {
        continue;
      }

      QColor blackKeyColor = frame.blackKeyColor;
      blackKeyColor.setAlpha(255);
      appendRect(geom.x, y, geom.width, blackHeight, blackKeyColor);

      const bool active = frame.playbackActive &&
                          track < static_cast<int>(frame.activeKeysByTrack.size()) &&
                          frame.activeKeysByTrack[static_cast<size_t>(track)].test(
                              static_cast<size_t>(key));
      if (active) {
        QColor activeBlack = trackColor.lighter(118);
        activeBlack.setAlpha(230);
        appendRect(geom.x + 1.0f,
                   y + 1.0f,
                   std::max(0.0f, geom.width - 2.0f),
                   std::max(0.0f, blackHeight - 2.0f),
                   activeBlack);
      }

      QColor blackEdge = frame.separatorColor;
      blackEdge.setAlpha(130);
      appendRect(geom.x, y + blackHeight - 1.0f, geom.width, 1.0f, blackEdge);
    }

    if (track < frame.trackCount - 1 && rowGap > 0.0f) {
      QColor rowGapColor = frame.separatorColor;
      rowGapColor.setAlpha(70);
      appendRect(0.0f, y + keyboardHeight, keyboardWidth, rowGap, rowGapColor);
    }
  }
}

void ActiveNoteRhiRenderer::appendWhiteKeyShape(float x,
                                                float y,
                                                float width,
                                                float fullHeight,
                                                float topHeight,
                                                bool hasLeftBlack,
                                                bool hasRightBlack,
                                                float notchWidth,
                                                const QColor& color) {
  if (width <= 0.0f || fullHeight <= 0.0f || color.alpha() <= 0) {
    return;
  }

  const float clampedTop = std::clamp(topHeight, 0.0f, fullHeight);
  const float lowerHeight = fullHeight - clampedTop;
  if (lowerHeight > 0.0f) {
    appendRect(x, y + clampedTop, width, lowerHeight, color);
  }

  if (clampedTop <= 0.0f) {
    return;
  }

  const float clampedNotch = std::clamp(notchWidth, 0.0f, width * 0.5f);
  if (hasLeftBlack && hasRightBlack) {
    appendRect(x + clampedNotch,
               y,
               std::max(0.0f, width - (2.0f * clampedNotch)),
               clampedTop,
               color);
  } else if (hasLeftBlack) {
    appendRect(x + clampedNotch,
               y,
               std::max(0.0f, width - clampedNotch),
               clampedTop,
               color);
  } else if (hasRightBlack) {
    appendRect(x, y, std::max(0.0f, width - clampedNotch), clampedTop, color);
  } else {
    appendRect(x, y, width, clampedTop, color);
  }
}

void ActiveNoteRhiRenderer::appendRect(float x, float y, float w, float h, const QColor& color) {
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
