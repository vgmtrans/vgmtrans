/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSize>
#include <QWindow>

#include <memory>

class QEvent;
class QExposeEvent;
class QResizeEvent;
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiSwapChain;

class SimpleRhiWindow : public QWindow {
public:
  SimpleRhiWindow();
  ~SimpleRhiWindow() override;

protected:
  bool event(QEvent* e) override;
  void exposeEvent(QExposeEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

  // Must be called by derived destructors while dynamic dispatch is valid.
  void releaseRhiResources();

  virtual bool handleWindowEvent(QEvent* e);
  virtual void onRhiInitialized(QRhi* rhi) = 0;
  virtual void onRhiReleased() = 0;
  virtual void renderRhiFrame(QRhiCommandBuffer* cb,
                              QRhiRenderTarget* renderTarget,
                              QRhiRenderPassDescriptor* renderPassDesc,
                              const QSize& pixelSize,
                              int sampleCount,
                              float dpr) = 0;

private:
  struct BackendData;

  void initIfNeeded();
  void resizeSwapChain();
  void renderFrame();
  void releaseResources(bool notifyDerived);
  void releaseSwapChain();

  std::unique_ptr<BackendData> m_backend;

  QRhi* m_rhi = nullptr;
  QRhiSwapChain* m_sc = nullptr;
  QRhiRenderBuffer* m_ds = nullptr;
  QRhiRenderPassDescriptor* m_rp = nullptr;

  bool m_hasSwapChain = false;
  bool m_resourcesReleased = false;
};
