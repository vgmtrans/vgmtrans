/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteRhiWindow.h"

#include "ActiveNoteRhiRenderer.h"
#include "ActiveNoteView.h"

#include <rhi/qrhi.h>

ActiveNoteRhiWindow::ActiveNoteRhiWindow(ActiveNoteView* view, ActiveNoteRhiRenderer* renderer)
    : m_view(view),
      m_renderer(renderer) {
}

ActiveNoteRhiWindow::~ActiveNoteRhiWindow() {
  releaseRhiResources();
}

void ActiveNoteRhiWindow::onRhiInitialized(QRhi* rhi) {
  if (m_renderer) {
    m_renderer->initIfNeeded(rhi);
  }
}

void ActiveNoteRhiWindow::onRhiReleased() {
  if (m_renderer) {
    m_renderer->releaseResources();
  }
}

void ActiveNoteRhiWindow::renderRhiFrame(QRhiCommandBuffer* cb,
                                         QRhiRenderTarget* renderTarget,
                                         QRhiRenderPassDescriptor* renderPassDesc,
                                         const QSize& pixelSize,
                                         int sampleCount,
                                         float dpr) {
  if (!m_view || !m_view->viewport() || !m_renderer) {
    return;
  }

  ActiveNoteRhiRenderer::RenderTargetInfo info;
  info.renderTarget = renderTarget;
  info.renderPassDesc = renderPassDesc;
  info.pixelSize = pixelSize;
  info.sampleCount = sampleCount;
  info.dpr = dpr;
  m_renderer->renderFrame(cb, info);
}
