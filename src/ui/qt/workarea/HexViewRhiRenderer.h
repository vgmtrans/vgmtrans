/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <array>
#include <cstdint>
#include <vector>

class QChar;
class QRectF;
class QVector4D;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiTextureRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class HexView;

class HexViewRhiRenderer {
public:
  struct RenderTargetInfo {
    QRhiRenderTarget* renderTarget = nullptr;
    QRhiRenderPassDescriptor* renderPassDesc = nullptr;
    QSize pixelSize;
    int sampleCount = 1;
    float dpr = 1.0f;
  };

  HexViewRhiRenderer(HexView* view, const char* logLabel);
  ~HexViewRhiRenderer();

  void initIfNeeded(QRhi* rhi);
  void releaseResources();

  void markBaseDirty();
  void markSelectionDirty();
  void invalidateCache();

  void renderFrame(QRhiCommandBuffer* cb, const RenderTargetInfo& target);

  bool isInited() const { return m_inited; }

private:
  static constexpr int kBytesPerLine = 16;

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

  struct EdgeInstance {
    float geomX;
    float geomY;
    float geomW;
    float geomH;
    float rectX;
    float rectY;
    float rectW;
    float rectH;
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

  struct CachedLine {
    int line = 0;
    int bytes = 0;
    std::array<uint8_t, kBytesPerLine> data{};
    std::array<uint16_t, kBytesPerLine> styles{};
  };

  struct LineRange {
    uint32_t rectStart = 0;
    uint32_t rectCount = 0;
    uint32_t glyphStart = 0;
    uint32_t glyphCount = 0;
  };

  void ensureRenderTargets(const QSize& pixelSize);
  void releaseRenderTargets();
  bool debugLoggingEnabled() const;

  void ensurePipelines(QRhiRenderPassDescriptor* outputRp, int outputSampleCount);
  void ensureGlyphTexture(QRhiResourceUpdateBatch* u);
  void ensureItemIdTexture(QRhiResourceUpdateBatch* u, int startLine, int endLine, int totalLines);
  void updateCompositeSrb();
  void updateUniforms(QRhiResourceUpdateBatch* u, float scrollY, const QSize& pixelSize);
  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes);
  void updateInstanceBuffers(QRhiResourceUpdateBatch* u);
  void drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                      int firstInstance = 0, QRhiShaderResourceBindings* srb = nullptr,
                      QRhiGraphicsPipeline* pso = nullptr);
  void drawEdgeBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                      int firstInstance = 0, QRhiShaderResourceBindings* srb = nullptr,
                      QRhiGraphicsPipeline* pso = nullptr);
  void drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count, int firstInstance = 0);
  void drawFullscreen(QRhiCommandBuffer* cb, QRhiGraphicsPipeline* pso,
                      QRhiShaderResourceBindings* srb);

  QRectF glyphUv(const QChar& ch) const;
  void appendRect(std::vector<RectInstance>& rects, float x, float y, float w, float h,
                  const QVector4D& color);
  void appendEdgeRect(std::vector<EdgeInstance>& rects, float x, float y, float w, float h,
                      float pad, const QVector4D& color);
  void appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w, float h,
                   const QRectF& uv, const QVector4D& color);

  void ensureCacheWindow(int startLine, int endLine, int totalLines);
  void rebuildCacheWindow();
  const CachedLine* cachedLineFor(int line) const;
  void buildBaseInstances();
  void buildSelectionInstances(int startLine, int endLine);

  HexView* m_view = nullptr;
  const char* m_logLabel = "HexViewRhi";

  QRhi* m_rhi = nullptr;

  QRhiTexture* m_contentTex = nullptr;
  QRhiTextureRenderTarget* m_contentRt = nullptr;
  QRhiRenderPassDescriptor* m_contentRp = nullptr;
  QRhiRenderPassDescriptor* m_outputRp = nullptr;
  QRhiTexture* m_maskTex = nullptr;
  QRhiTextureRenderTarget* m_maskRt = nullptr;
  QRhiRenderPassDescriptor* m_maskRp = nullptr;
  QRhiTexture* m_edgeTex = nullptr;
  QRhiTextureRenderTarget* m_edgeRt = nullptr;
  QRhiRenderPassDescriptor* m_edgeRp = nullptr;

  QRhiBuffer* m_vbuf = nullptr;
  QRhiBuffer* m_ibuf = nullptr;
  QRhiBuffer* m_baseRectBuf = nullptr;
  QRhiBuffer* m_baseGlyphBuf = nullptr;
  QRhiBuffer* m_maskRectBuf = nullptr;
  QRhiBuffer* m_edgeRectBuf = nullptr;
  QRhiBuffer* m_ubuf = nullptr;
  QRhiBuffer* m_edgeUbuf = nullptr;
  QRhiBuffer* m_compositeUbuf = nullptr;
  QRhiTexture* m_glyphTex = nullptr;
  QRhiTexture* m_itemIdTex = nullptr;
  QRhiSampler* m_glyphSampler = nullptr;
  QRhiSampler* m_maskSampler = nullptr;
  QRhiShaderResourceBindings* m_rectSrb = nullptr;
  QRhiShaderResourceBindings* m_glyphSrb = nullptr;
  QRhiShaderResourceBindings* m_edgeSrb = nullptr;
  QRhiShaderResourceBindings* m_compositeSrb = nullptr;
  QRhiGraphicsPipeline* m_rectPso = nullptr;
  QRhiGraphicsPipeline* m_glyphPso = nullptr;
  QRhiGraphicsPipeline* m_maskPso = nullptr;
  QRhiGraphicsPipeline* m_edgePso = nullptr;
  QRhiGraphicsPipeline* m_compositePso = nullptr;
  int m_sampleCount = 1;
  uint64_t m_glyphAtlasVersion = 0;
  bool m_staticBuffersUploaded = false;
  bool m_pipelinesDirty = true;
  bool m_supportsBaseInstance = false;
  bool m_loggedInit = false;
  bool m_loggedFrame = false;
  QElapsedTimer m_animTimer;

  std::vector<CachedLine> m_cachedLines;
  std::vector<LineRange> m_lineRanges;
  std::vector<RectInstance> m_baseRectInstances;
  std::vector<GlyphInstance> m_baseGlyphInstances;
  std::vector<RectInstance> m_maskRectInstances;
  std::vector<EdgeInstance> m_edgeRectInstances;
  int m_cacheStartLine = 0;
  int m_cacheEndLine = -1;
  int m_lastStartLine = -1;
  int m_lastEndLine = -1;
  bool m_baseDirty = true;
  bool m_selectionDirty = true;
  bool m_baseBufferDirty = false;
  bool m_selectionBufferDirty = false;
  bool m_itemIdDirty = true;
  bool m_outlineEnabled = false;
  float m_outlineAlpha = 0.0f;
  float m_lastFrameSeconds = 0.0f;
  float m_outlineTarget = 0.0f;
  int m_itemIdStartLine = 0;
  QSize m_itemIdSize;
  bool m_compositeSrbDirty = false;
  bool m_inited = false;
};
