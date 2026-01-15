/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWindow>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class QChar;
class QEvent;
class QExposeEvent;
class QRectF;
class QResizeEvent;
class QVector4D;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiSwapChain;
class QRhiTexture;
class HexView;

class HexViewRhiWindow final : public QWindow {
  Q_OBJECT

public:
  explicit HexViewRhiWindow(HexView* view);
  ~HexViewRhiWindow() override;

  void markBaseDirty();
  void markSelectionDirty();
  void markOverlayDirty();
  void invalidateCache();

protected:
  bool event(QEvent* e) override;
  void exposeEvent(QExposeEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

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

  struct ShadowInstance {
    float gx;
    float gy;
    float gw;
    float gh;
    float rx;
    float ry;
    float rw;
    float rh;
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

  struct BackendData;

  void initIfNeeded();
  void resizeSwapChain();
  void renderFrame();
  void releaseResources();

  void ensurePipelines();
  void ensureGlyphTexture(QRhiResourceUpdateBatch* u);
  void updateUniforms(QRhiResourceUpdateBatch* u, float scrollY);
  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes);
  void updateInstanceBuffers(QRhiResourceUpdateBatch* u);
  void drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                      int firstInstance = 0, QRhiShaderResourceBindings* srb = nullptr);
  void drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count, int firstInstance = 0);
  void drawShadowBuffer(QRhiCommandBuffer* cb);

  QRectF glyphUv(const QChar& ch) const;
  void appendRect(std::vector<RectInstance>& rects, float x, float y, float w, float h,
                  const QVector4D& color);
  void appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w, float h,
                   const QRectF& uv, const QVector4D& color);
  void appendShadow(const QRectF& rect, float pad, const QVector4D& color);

  void ensureCacheWindow(int startLine, int endLine, int totalLines);
  void rebuildCacheWindow();
  const CachedLine* cachedLineFor(int line) const;
  void buildBaseInstances();
  void buildOverlayInstances(int viewportHeight);
  void buildSelectionInstances(int startLine, int endLine);

  HexView* m_view = nullptr;

  std::unique_ptr<BackendData> m_backend;

  QRhi* m_rhi = nullptr;
  QRhiSwapChain* m_sc = nullptr;
  QRhiRenderBuffer* m_ds = nullptr;
  QRhiRenderPassDescriptor* m_rp = nullptr;
  QRhiCommandBuffer* m_cb = nullptr;

  QRhiBuffer* m_vbuf = nullptr;
  QRhiBuffer* m_ibuf = nullptr;
  QRhiBuffer* m_baseRectBuf = nullptr;
  QRhiBuffer* m_baseGlyphBuf = nullptr;
  QRhiBuffer* m_overlayRectBuf = nullptr;
  QRhiBuffer* m_selectionRectBuf = nullptr;
  QRhiBuffer* m_selectionGlyphBuf = nullptr;
  QRhiBuffer* m_shadowBuf = nullptr;
  QRhiBuffer* m_ubuf = nullptr;
  QRhiBuffer* m_overlayUbuf = nullptr;
  QRhiBuffer* m_shadowUbuf = nullptr;
  QRhiTexture* m_glyphTex = nullptr;
  QRhiSampler* m_glyphSampler = nullptr;
  QRhiShaderResourceBindings* m_rectSrb = nullptr;
  QRhiShaderResourceBindings* m_overlaySrb = nullptr;
  QRhiShaderResourceBindings* m_glyphSrb = nullptr;
  QRhiShaderResourceBindings* m_shadowSrb = nullptr;
  QRhiGraphicsPipeline* m_rectPso = nullptr;
  QRhiGraphicsPipeline* m_glyphPso = nullptr;
  QRhiGraphicsPipeline* m_shadowPso = nullptr;
  int m_sampleCount = 1;
  uint64_t m_glyphAtlasVersion = 0;
  bool m_staticBuffersUploaded = false;

  std::vector<CachedLine> m_cachedLines;
  std::vector<LineRange> m_lineRanges;
  std::vector<RectInstance> m_baseRectInstances;
  std::vector<GlyphInstance> m_baseGlyphInstances;
  std::vector<RectInstance> m_overlayRectInstances;
  std::vector<RectInstance> m_selectionRectInstances;
  std::vector<GlyphInstance> m_selectionGlyphInstances;
  std::vector<ShadowInstance> m_shadowInstances;
  int m_cacheStartLine = 0;
  int m_cacheEndLine = -1;
  int m_lastStartLine = -1;
  int m_lastEndLine = -1;
  bool m_baseDirty = true;
  bool m_selectionDirty = true;
  bool m_overlayDirty = true;
  bool m_baseBufferDirty = false;
  bool m_selectionBufferDirty = false;
  bool m_overlayBufferDirty = false;
  bool m_inited = false;
};
