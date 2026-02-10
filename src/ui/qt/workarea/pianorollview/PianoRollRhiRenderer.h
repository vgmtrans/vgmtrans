/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSize>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "PianoRollFrameData.h"

class QColor;
class QMatrix4x4;
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
class QRhiTextureRenderTarget;

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
    bool valid = false;
    QSize viewSize;
    int totalTicks = 0;
    int ppqn = 0;
    int pixelsPerTickQ = 0;
    int pixelsPerKeyQ = 0;
    int keyboardWidth = 0;
    int topBarHeight = 0;
    uint64_t notesPtr = 0;
    uint64_t timeSigPtr = 0;
    uint64_t trackColorsHash = 0;
    uint32_t noteBackgroundColor = 0;
    uint32_t keyboardBackgroundColor = 0;
    uint32_t topBarBackgroundColor = 0;
    uint32_t measureLineColor = 0;
    uint32_t beatLineColor = 0;
    uint32_t keySeparatorColor = 0;
    uint32_t noteOutlineColor = 0;
    uint32_t whiteKeyColor = 0;
    uint32_t blackKeyColor = 0;
    uint32_t whiteKeyRowColor = 0;
    uint32_t blackKeyRowColor = 0;
    uint32_t dividerColor = 0;
  };

  struct RectInstance {
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
    float scrollMulX;
    float scrollMulY;
  };

  struct TileInstance {
    float x;
    float y;
    float w;
    float h;
    float u;
    float v;
    float uw;
    float vh;
  };

  enum class StaticPlane : int {
    XY = 0,
    X = 1,
    Y = 2,
    Count = 3,
  };

  struct TileRange {
    bool valid = false;
    int minX = 0;
    int maxX = -1;
    int minY = 0;
    int maxY = -1;
  };

  struct StaticTile {
    int tileX = 0;
    int tileY = 0;
    QRhiTexture* texture = nullptr;
    QRhiTextureRenderTarget* renderTarget = nullptr;
    QRhiShaderResourceBindings* compositeSrb = nullptr;
    bool ready = false;
    uint64_t generation = 0;
  };

  struct TilePlaneCache {
    std::unordered_map<uint64_t, StaticTile> tiles;
    std::vector<uint64_t> visibleKeys;
    TileRange keepRange;
  };

  enum class LineStyle : int {
    Solid = 0,
    DottedVertical = 1,
  };

  static bool isBlackKey(int key);
  static uint64_t hashTrackColors(const std::vector<QColor>& colors);
  static uint32_t colorKey(const QColor& color);

  static int planeIndex(StaticPlane plane);
  static uint64_t makeTileKey(int tileX, int tileY);
  static void fillUniformData(std::array<float, 20>& ubo,
                              const QMatrix4x4& mvp,
                              float cameraX,
                              float cameraY);

  void ensurePipelines(QRhiRenderPassDescriptor* renderPassDesc, int sampleCount);
  void ensureTilePipeline();
  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes, int minBytes);
  Layout computeLayout(const PianoRollFrame::Data& frame, const QSize& pixelSize) const;
  QSize tilePixelSizeForDpr(float dpr) const;
  TileRange tileRangeForPlane(const Layout& layout, StaticPlane plane, int margin) const;
  void updateVisibleTiles(const Layout& layout, float dpr);
  void pruneTileCaches();
  bool ensureTile(StaticPlane plane, int tileX, int tileY, float dpr);
  void renderMissingTiles(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch*& initialUpdates);
  void releaseTileCaches();
  std::vector<RectInstance>& staticPlaneInstances(StaticPlane plane);
  const std::vector<RectInstance>& staticPlaneInstances(StaticPlane plane) const;
  QRhiBuffer*& staticPlaneBuffer(StaticPlane plane);

  StaticCacheKey makeStaticCacheKey(const PianoRollFrame::Data& frame,
                                    const Layout& layout,
                                    uint64_t trackColorsHash) const;
  bool staticCacheKeyEquals(const StaticCacheKey& a, const StaticCacheKey& b) const;

  void buildStaticInstances(const PianoRollFrame::Data& frame,
                            const Layout& layout,
                            uint64_t trackColorsHash);
  void buildDynamicInstances(const PianoRollFrame::Data& frame, const Layout& layout);

  template <typename Fn>
  void forEachVisibleNote(const PianoRollFrame::Data& frame, const Layout& layout, Fn&& fn) const;

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
                  float scrollMulY = 0.0f);

  PianoRollView* m_view = nullptr;

  QRhi* m_rhi = nullptr;
  QRhiBuffer* m_vertexBuffer = nullptr;
  QRhiBuffer* m_indexBuffer = nullptr;
  QRhiBuffer* m_uniformBuffer = nullptr;
  QRhiBuffer* m_tileUniformBuffer = nullptr;
  QRhiBuffer* m_staticFixedInstanceBuffer = nullptr;
  QRhiBuffer* m_staticXyInstanceBuffer = nullptr;
  QRhiBuffer* m_staticXInstanceBuffer = nullptr;
  QRhiBuffer* m_staticYInstanceBuffer = nullptr;
  QRhiBuffer* m_dynamicInstanceBuffer = nullptr;
  QRhiBuffer* m_tileInstanceBuffer = nullptr;

  QRhiShaderResourceBindings* m_outputRectSrb = nullptr;
  QRhiShaderResourceBindings* m_tileRectSrb = nullptr;
  QRhiShaderResourceBindings* m_tileCompositeLayoutSrb = nullptr;
  QRhiGraphicsPipeline* m_outputRectPipeline = nullptr;
  QRhiGraphicsPipeline* m_tileRectPipeline = nullptr;
  QRhiGraphicsPipeline* m_tileCompositePipeline = nullptr;
  QRhiSampler* m_tileSampler = nullptr;
  QRhiTexture* m_tileDummyTexture = nullptr;
  QRhiRenderPassDescriptor* m_tileRenderPass = nullptr;

  QRhiRenderPassDescriptor* m_outputRenderPass = nullptr;
  int m_sampleCount = 1;
  QSize m_tilePixelSize;
  uint64_t m_staticGeneration = 0;
  bool m_geometryUploaded = false;
  bool m_staticInstanceBuffersDirty = true;
  bool m_inited = false;
  StaticCacheKey m_staticCacheKey;

  std::vector<RectInstance> m_staticFixedInstances;
  std::array<std::vector<RectInstance>, static_cast<int>(StaticPlane::Count)> m_staticPlaneInstanceData;
  std::vector<RectInstance> m_dynamicInstances;
  std::array<TilePlaneCache, static_cast<int>(StaticPlane::Count)> m_tilePlaneCaches;
  std::vector<TileInstance> m_visibleTileInstances;
  std::vector<QRhiShaderResourceBindings*> m_visibleTileSrbs;
};
