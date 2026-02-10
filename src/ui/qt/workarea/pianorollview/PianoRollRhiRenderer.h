/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSize>

#include <vector>

#include "PianoRollFrameData.h"

class QColor;
class PianoRollView;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiShaderResourceBindings;

class PianoRollRhiRenderer {
public:
  struct RenderTargetInfo {
    QRhiRenderTarget* renderTarget = nullptr;
    QRhiRenderPassDescriptor* renderPassDesc = nullptr;
    QSize pixelSize;
    int sampleCount = 1;
    float dpr = 1.0f;
  };

  explicit PianoRollRhiRenderer(PianoRollView* view);
  ~PianoRollRhiRenderer();

  void initIfNeeded(QRhi* rhi);
  void releaseResources();
  void renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target);

private:
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

  static bool isBlackKey(int key);

  void ensurePipeline(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount);
  bool ensureInstanceBuffer(int bytes);
  void buildInstances(const PianoRollFrame::Data& frame, const QSize& pixelSize);
  void appendRect(float x, float y, float w, float h, const QColor& color);
  void appendDottedLine(float x,
                        float y,
                        float h,
                        float segmentHeight,
                        float gapHeight,
                        const QColor& color);

  PianoRollView* m_view = nullptr;

  QRhi* m_rhi = nullptr;
  QRhiBuffer* m_vertexBuffer = nullptr;
  QRhiBuffer* m_indexBuffer = nullptr;
  QRhiBuffer* m_uniformBuffer = nullptr;
  QRhiBuffer* m_instanceBuffer = nullptr;
  QRhiShaderResourceBindings* m_shaderBindings = nullptr;
  QRhiGraphicsPipeline* m_pipeline = nullptr;

  QRhiRenderPassDescriptor* m_outputRenderPass = nullptr;
  int m_sampleCount = 1;
  bool m_staticBuffersUploaded = false;
  bool m_inited = false;

  std::vector<RectInstance> m_instances;
};
