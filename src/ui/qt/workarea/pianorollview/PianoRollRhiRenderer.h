/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSize>

#include <array>
#include <cstdint>
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
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

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
  struct Layout {
    int viewWidth = 0;
    int viewHeight = 0;
    float keyboardWidth = 0.0f;
    float topBarHeight = 0.0f;
    float noteAreaLeft = 0.0f;
    float noteAreaTop = 0.0f;
    float noteAreaWidth = 0.0f;
    float noteAreaHeight = 0.0f;
    float pixelsPerTick = 1.0f;
    float pixelsPerKey = 1.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    uint64_t visibleStartTick = 0;
    uint64_t visibleEndTick = 0;
  };

  struct NoteGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool valid = false;
  };

  struct StaticCacheKey {
    QSize viewSize;
    int totalTicks = 0;
    int ppqn = 0;
    int keyboardWidth = 0;
    int topBarHeight = 0;
    uint64_t timeSigPtr = 0;
    uint32_t noteBackgroundColor = 0;
    uint32_t keyboardBackgroundColor = 0;
    uint32_t topBarBackgroundColor = 0;
    uint32_t measureLineColor = 0;
    uint32_t beatLineColor = 0;
    uint32_t keySeparatorColor = 0;
    uint32_t whiteKeyColor = 0;
    uint32_t blackKeyColor = 0;
    uint32_t whiteKeyRowColor = 0;
    uint32_t blackKeyRowColor = 0;
    uint32_t dividerColor = 0;
  };

  struct NoteInstance {
    float startTick;
    float duration;
    float key;
    float borderEnabled;
    float r;
    float g;
    float b;
    float a;
  };

  struct NoteDataKey {
    uint64_t notesPtr = 0;
    uint64_t trackColorsHash = 0;
    uint64_t trackEnabledHash = 0;
    uint32_t noteBackgroundColor = 0;
  };

  struct LabelGlyph {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float width = 0.0f;
    float advance = 0.0f;
  };

  struct RectInstance {
    // Per-instance payload consumed by pianorollquad shaders.
    float x;
    float y;
    float w;
    float h;
    float r;
    float g;
    float b;
    float a;
    float style;
    float segment;
    float gap;
    float phase;
    float posModeX;
    float posModeY;
    float sizeModeX;
    float sizeModeY;
    float scrollMulX;
    float scrollMulY;
  };

  enum class LineStyle : int {
    Solid = 0,
    DottedVertical = 1,
    GridMeasure = 2,
    GridBeat = 3,
    TriangleDown = 4,
    TopBarGradient = 5,
    MeasureNumber = 6,
  };

  static bool isBlackKey(int key);
  static uint64_t hashTrackColors(const std::vector<QColor>& colors);
  static uint64_t hashTrackEnabled(const std::vector<uint8_t>& trackEnabled);
  static uint32_t colorKey(const QColor& color);

  void ensurePipelines(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount);
  void ensureMeasureLabelAtlas(QRhiResourceUpdateBatch* updates);
  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes, int minBytes);
  Layout computeLayout(const PianoRollFrame::Data& frame, const QSize& pixelSize) const;
  StaticCacheKey makeStaticCacheKey(const PianoRollFrame::Data& frame, const Layout& layout) const;
  static bool staticCacheKeyEqual(const StaticCacheKey& lhs, const StaticCacheKey& rhs);
  NoteDataKey makeNoteDataKey(const PianoRollFrame::Data& frame) const;
  void rebuildNoteInstances(const PianoRollFrame::Data& frame);
  void appendMeasureNumberOverlays(const PianoRollFrame::Data& frame, const Layout& layout);

  void buildStaticInstances(const PianoRollFrame::Data& frame, const Layout& layout);
  void buildDynamicInstances(const PianoRollFrame::Data& frame, const Layout& layout);

  NoteGeometry computeNoteGeometry(const PianoRollFrame::Note& note, const Layout& layout) const;

  void appendRect(std::vector<RectInstance>& instances,
                  float x,
                  float y,
                  float w,
                  float h,
                  const QColor& color,
                  LineStyle style = LineStyle::Solid,
                  float segment = 0.0f,
                  float gap = 0.0f,
                  float phase = 0.0f,
                  float scrollMulX = 0.0f,
                  float scrollMulY = 0.0f,
                  float posModeX = 0.0f,
                  float posModeY = 0.0f,
                  float sizeModeX = 0.0f,
                  float sizeModeY = 0.0f);

  PianoRollView* m_view = nullptr;

  QRhi* m_rhi = nullptr;
  QRhiBuffer* m_vertexBuffer = nullptr;
  QRhiBuffer* m_indexBuffer = nullptr;
  QRhiBuffer* m_uniformBuffer = nullptr;
  QRhiBuffer* m_staticBackInstanceBuffer = nullptr;
  QRhiBuffer* m_staticFrontInstanceBuffer = nullptr;
  QRhiBuffer* m_dynamicInstanceBuffer = nullptr;
  QRhiBuffer* m_noteInstanceBuffer = nullptr;
  QRhiTexture* m_measureLabelAtlas = nullptr;
  QRhiSampler* m_measureLabelSampler = nullptr;
  QRhiShaderResourceBindings* m_shaderBindings = nullptr;
  QRhiGraphicsPipeline* m_pipeline = nullptr;
  QRhiGraphicsPipeline* m_notePipeline = nullptr;

  QRhiRenderPassDescriptor* m_outputRenderPass = nullptr;
  int m_sampleCount = 1;
  bool m_staticBuffersUploaded = false;
  bool m_inited = false;
  bool m_measureLabelAtlasDirty = true;
  float m_measureLabelHeight = 0.0f;
  std::array<LabelGlyph, 10> m_measureLabelDigits{};
  std::vector<RectInstance> m_staticBackInstances;
  std::vector<RectInstance> m_staticFrontInstances;
  std::vector<RectInstance> m_dynamicInstances;
  std::vector<NoteInstance> m_noteInstances;
  int m_dynamicFrontStart = 0;

  bool m_hasStaticCacheKey = false;
  StaticCacheKey m_staticCacheKey;
  bool m_staticDataDirty = true;
  bool m_noteDataDirty = true;
  bool m_hasNoteDataKey = false;
  NoteDataKey m_noteDataKey;
};
