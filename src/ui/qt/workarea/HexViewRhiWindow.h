/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QList>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QPointF>
#include <memory>

#include "HexViewRhiInputCoalescer.h"

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
  explicit HexViewRhiWindow(HexView* view, HexViewRhiRenderer* renderer);
  ~HexViewRhiWindow() override;

protected:
  bool event(QEvent* e) override;
  void exposeEvent(QExposeEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

signals:
  void dragOverlayShowRequested();
  void dragOverlayHideRequested();
  void dropUrlsRequested(const QList<QUrl>& urls);

private:
  struct BackendData;

  void initIfNeeded();
  void resizeSwapChain();
  void renderFrame();
  void releaseResources();
  void releaseSwapChain();
  bool debugLoggingEnabled() const;

  HexView* m_view = nullptr;
  HexViewRhiRenderer* m_renderer = nullptr;

  std::unique_ptr<BackendData> m_backend;

  QRhi* m_rhi = nullptr;
  QRhiSwapChain* m_sc = nullptr;
  QRhiRenderBuffer* m_ds = nullptr;
  QRhiRenderPassDescriptor* m_rp = nullptr;
  QRhiCommandBuffer* m_cb = nullptr;

  bool m_dragging = false;
  HexViewRhiInputCoalescer m_input;
  int m_pumpFrames = 0;        // generic "keep drawing for N frames"
  QTimer m_scrollBarFrameTimer;

  bool m_hasSwapChain = false;
};
