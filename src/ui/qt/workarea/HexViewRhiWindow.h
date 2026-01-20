/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWindow>
#include <QPointF>
#include <memory>

class QEvent;
class QExposeEvent;
class QResizeEvent;
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiSwapChain;
class HexView;
class HexViewRhiRenderer;

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
  struct BackendData;

  void initIfNeeded();
  void resizeSwapChain();
  void renderFrame();
  void releaseResources();
  void releaseSwapChain();
  bool debugLoggingEnabled() const;

  void drainPendingMouseMove();
  void drainPendingWheel();

  HexView* m_view = nullptr;

  std::unique_ptr<BackendData> m_backend;
  std::unique_ptr<HexViewRhiRenderer> m_renderer;

  QRhi* m_rhi = nullptr;
  QRhiSwapChain* m_sc = nullptr;
  QRhiRenderBuffer* m_ds = nullptr;
  QRhiRenderPassDescriptor* m_rp = nullptr;
  QRhiCommandBuffer* m_cb = nullptr;

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

  bool m_scrolling = false;
  int m_pumpFrames = 0;        // generic "keep drawing for N frames"

  bool m_hasSwapChain = false;
};
