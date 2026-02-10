/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "workarea/rhi/SimpleRhiWindow.h"

class ActiveNoteRhiRenderer;
class ActiveNoteView;
class QRhi;

class ActiveNoteRhiWindow final : public SimpleRhiWindow {
public:
  explicit ActiveNoteRhiWindow(ActiveNoteView* view, ActiveNoteRhiRenderer* renderer);
  ~ActiveNoteRhiWindow() override;

protected:
  void onRhiInitialized(QRhi* rhi) override;
  void onRhiReleased() override;
  void renderRhiFrame(QRhiCommandBuffer* cb,
                      QRhiRenderTarget* renderTarget,
                      QRhiRenderPassDescriptor* renderPassDesc,
                      const QSize& pixelSize,
                      int sampleCount,
                      float dpr) override;

private:
  ActiveNoteView* m_view = nullptr;
  ActiveNoteRhiRenderer* m_renderer = nullptr;
};
