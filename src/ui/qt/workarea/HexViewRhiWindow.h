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
class QSize;
class QVector4D;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTextureRenderTarget;
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
  void ensureRenderTargets(const QSize& pixelSize);
  void releaseRenderTargets();
  bool debugLoggingEnabled() const;

  void ensurePipelines();
  void ensureGlyphTexture(QRhiResourceUpdateBatch* u);
  void updateUniforms(QRhiResourceUpdateBatch* u, float scrollY, const QSize& pixelSize);
  bool ensureInstanceBuffer(QRhiBuffer*& buffer, int bytes);
  void updateInstanceBuffers(QRhiResourceUpdateBatch* u);
  void drawRectBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count,
                      int firstInstance = 0, QRhiShaderResourceBindings* srb = nullptr,
                      QRhiGraphicsPipeline* pso = nullptr);
  void drawGlyphBuffer(QRhiCommandBuffer* cb, QRhiBuffer* buffer, int count, int firstInstance = 0);
  void drawFullscreen(QRhiCommandBuffer* cb, QRhiGraphicsPipeline* pso,
                      QRhiShaderResourceBindings* srb);

  QRectF glyphUv(const QChar& ch) const;
  void appendRect(std::vector<RectInstance>& rects, float x, float y, float w, float h,
                  const QVector4D& color);
  void appendGlyph(std::vector<GlyphInstance>& glyphs, float x, float y, float w, float h,
                   const QRectF& uv, const QVector4D& color);

  void ensureCacheWindow(int startLine, int endLine, int totalLines);
  void rebuildCacheWindow();
  const CachedLine* cachedLineFor(int line) const;
  void buildBaseInstances();
  void buildSelectionInstances(int startLine, int endLine);

  void releaseSwapChain();

  HexView* m_view = nullptr;

  std::unique_ptr<BackendData> m_backend;

  QRhi* m_rhi = nullptr;
  QRhiSwapChain* m_sc = nullptr;
  QRhiRenderBuffer* m_ds = nullptr;
  QRhiRenderPassDescriptor* m_rp = nullptr;
  QRhiCommandBuffer* m_cb = nullptr;

  QRhiTexture* m_contentTex = nullptr;
  QRhiTextureRenderTarget* m_contentRt = nullptr;
  QRhiRenderPassDescriptor* m_contentRp = nullptr;
  QRhiTexture* m_maskTex = nullptr;
  QRhiTextureRenderTarget* m_maskRt = nullptr;
  QRhiRenderPassDescriptor* m_maskRp = nullptr;
  QRhiTexture* m_shadowTexA = nullptr;
  QRhiTexture* m_shadowTexB = nullptr;
  QRhiTextureRenderTarget* m_shadowRtA = nullptr;
  QRhiTextureRenderTarget* m_shadowRtB = nullptr;
  QRhiRenderPassDescriptor* m_shadowRpA = nullptr;
  QRhiRenderPassDescriptor* m_shadowRpB = nullptr;

  QRhiBuffer* m_vbuf = nullptr;
  QRhiBuffer* m_ibuf = nullptr;
  QRhiBuffer* m_baseRectBuf = nullptr;
  QRhiBuffer* m_baseGlyphBuf = nullptr;
  QRhiBuffer* m_maskRectBuf = nullptr;
  QRhiBuffer* m_ubuf = nullptr;
  QRhiBuffer* m_blurUbufH = nullptr;
  QRhiBuffer* m_blurUbufV = nullptr;
  QRhiBuffer* m_compositeUbuf = nullptr;
  QRhiTexture* m_glyphTex = nullptr;
  QRhiSampler* m_glyphSampler = nullptr;
  QRhiSampler* m_maskSampler = nullptr;
  QRhiShaderResourceBindings* m_rectSrb = nullptr;
  QRhiShaderResourceBindings* m_glyphSrb = nullptr;
  QRhiShaderResourceBindings* m_blurSrbH = nullptr;
  QRhiShaderResourceBindings* m_blurSrbV = nullptr;
  QRhiShaderResourceBindings* m_compositeSrb = nullptr;
  QRhiGraphicsPipeline* m_rectPso = nullptr;
  QRhiGraphicsPipeline* m_glyphPso = nullptr;
  QRhiGraphicsPipeline* m_maskPso = nullptr;
  QRhiGraphicsPipeline* m_blurPso = nullptr;
  QRhiGraphicsPipeline* m_compositePso = nullptr;
  int m_sampleCount = 1;
  uint64_t m_glyphAtlasVersion = 0;
  bool m_staticBuffersUploaded = false;
  bool m_pipelinesDirty = true;
  bool m_supportsBaseInstance = false;
  bool m_loggedInit = false;
  bool m_loggedFrame = false;

  std::vector<CachedLine> m_cachedLines;
  std::vector<LineRange> m_lineRanges;
  std::vector<RectInstance> m_baseRectInstances;
  std::vector<GlyphInstance> m_baseGlyphInstances;
  std::vector<RectInstance> m_maskRectInstances;
  int m_cacheStartLine = 0;
  int m_cacheEndLine = -1;
  int m_lastStartLine = -1;
  int m_lastEndLine = -1;
  bool m_baseDirty = true;
  bool m_selectionDirty = true;
  bool m_baseBufferDirty = false;
  bool m_selectionBufferDirty = false;
  bool m_inited = false;

  bool m_dragging = false;
  bool m_pendingMouseMove = false;

  // Mouse Move state
  QPointF m_pendingGlobalPos;
  Qt::MouseButtons m_pendingButtons = Qt::NoButton;
  Qt::KeyboardModifiers m_pendingMods = Qt::NoModifier;

  // Coalesced wheel state
  bool m_pendingWheel = false;
  QPointF m_wheelGlobalPos;
  QPoint m_wheelPixelDelta;   // summed
  QPoint m_wheelAngleDelta;   // summed
  Qt::KeyboardModifiers m_wheelMods = Qt::NoModifier;
  Qt::MouseButtons m_wheelButtons = Qt::NoButton;
  Qt::ScrollPhase m_wheelPhase = Qt::NoScrollPhase;
  void drainPendingMouseMove();

  bool m_scrolling = false;
  int m_pumpFrames = 0;        // generic "keep drawing for N frames"
  void drainPendingWheel();

  bool m_hasSwapChain = false;
};
