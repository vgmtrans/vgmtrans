/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollRhiHost.h"

#include "PianoRollRhiRenderer.h"
#include "PianoRollView.h"
#include "workarea/rhi/RhiWindowMainWindowLocator.h"
#include "workarea/rhi/SimpleRhiWindow.h"

#if defined(Q_OS_LINUX)
#include "PianoRollRhiWidget.h"
#else
#include "PianoRollRhiWindow.h"
#include "MainWindow.h"
#endif

#include <QResizeEvent>
#include <QWindow>

PianoRollRhiHost::PianoRollRhiHost(PianoRollView* view, QWidget* parent)
    : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);

  m_renderer = std::make_unique<PianoRollRhiRenderer>(view);

#if defined(Q_OS_LINUX)
  // QRhiWidget backend is stable on Linux.
  auto* widget = new PianoRollRhiWidget(view, m_renderer.get(), this);
  m_surface = widget;
#else
  // Native RHI window backend avoids QRhiWidget performance issues seen on
  // macOS/Windows with multiple active views.
  auto* window = new PianoRollRhiWindow(view, m_renderer.get());
  m_window = window;
  m_surface = QWidget::createWindowContainer(window, this);

  if (auto* mainWindow = QtUi::findMainWindowForWidget(this)) {
    connect(window, &SimpleRhiWindow::dragOverlayShowRequested,
            mainWindow, &MainWindow::showDragOverlay);
    connect(window, &SimpleRhiWindow::dragOverlayHideRequested,
            mainWindow, &MainWindow::hideDragOverlay);
    connect(window, &SimpleRhiWindow::dropUrlsRequested,
            mainWindow, &MainWindow::handleDroppedUrls);
  }
#endif

  if (m_surface) {
    m_surface->setFocusPolicy(Qt::NoFocus);
    m_surface->setMouseTracking(true);
    m_surface->setAttribute(Qt::WA_AcceptTouchEvents, true);
    m_surface->setGeometry(rect());
    m_surface->show();
  }
}

PianoRollRhiHost::~PianoRollRhiHost() {
  // Destroy the container/window first; renderer resources are owned and
  // released by the concrete surface implementation.
  delete m_surface;
  m_surface = nullptr;
  m_window = nullptr;
}

void PianoRollRhiHost::requestUpdate() {
  if (m_window) {
    m_window->requestUpdate();
  } else if (m_surface) {
    m_surface->update();
  }
}

void PianoRollRhiHost::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_surface) {
    // Keep the rendering surface perfectly aligned with the host viewport.
    m_surface->setGeometry(rect());
  }
}
