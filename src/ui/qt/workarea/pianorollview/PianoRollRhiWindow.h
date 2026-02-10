/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "workarea/rhi/SimpleRhiWindow.h"

class PianoRollRhiRenderer;
class PianoRollView;
class QRhi;

class PianoRollRhiWindow final : public SimpleRhiWindow {
public:
  explicit PianoRollRhiWindow(PianoRollView* view, PianoRollRhiRenderer* renderer);
  ~PianoRollRhiWindow() override = default;

protected:
  bool handleWindowEvent(QEvent* e) override;
  void onRhiInitialized(QRhi* rhi) override;
  void onRhiReleased() override;
  void renderRhiFrame(QRhiCommandBuffer* cb,
                      QRhiRenderTarget* renderTarget,
                      QRhiRenderPassDescriptor* renderPassDesc,
                      const QSize& pixelSize,
                      int sampleCount,
                      float dpr) override;

private:
  PianoRollView* m_view = nullptr;
  PianoRollRhiRenderer* m_renderer = nullptr;
};
