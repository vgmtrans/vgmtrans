/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiHost.h"

#include "HexView.h"
#include "HexViewRhiRenderer.h"
#include "workarea/rhi/RhiWindowMainWindowLocator.h"

#if defined(Q_OS_LINUX)
#include "HexViewRhiWidget.h"
#else
#include "HexViewRhiWindow.h"
#include "MainWindow.h"
#endif

#include <QWindow>
#include <QResizeEvent>

HexViewRhiHost::HexViewRhiHost(HexView* view, QWidget* parent)
    : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);

#if defined(Q_OS_LINUX)
  m_renderer = std::make_unique<HexViewRhiRenderer>(view, "HexViewRhiWidget");
  auto* widget = new HexViewRhiWidget(view, m_renderer.get(), this);
  m_surface = widget;
#else
  m_renderer = std::make_unique<HexViewRhiRenderer>(view, "HexViewRhiWindow");
  auto* window = new HexViewRhiWindow(view, m_renderer.get());
  m_window = window;
  m_surface = QWidget::createWindowContainer(window, this);

  auto* rhiWindow = qobject_cast<HexViewRhiWindow*>(m_window);
  if (rhiWindow) {
    MainWindow* mainWindow = QtUi::findMainWindowForWidget(this);
    if (mainWindow) {
      connect(rhiWindow, &HexViewRhiWindow::dragOverlayShowRequested,
              mainWindow, &MainWindow::showDragOverlay);
      connect(rhiWindow, &HexViewRhiWindow::dragOverlayHideRequested,
              mainWindow, &MainWindow::hideDragOverlay);
      connect(rhiWindow, &HexViewRhiWindow::dropUrlsRequested,
              mainWindow, &MainWindow::handleDroppedUrls);
    }
  }
#endif

  if (m_surface) {
    m_surface->setFocusPolicy(Qt::NoFocus);
    m_surface->setGeometry(rect());
    m_surface->show();
  }
}

HexViewRhiHost::~HexViewRhiHost() {
  delete m_surface;
  m_surface = nullptr;
  m_window = nullptr;
}

void HexViewRhiHost::markBaseDirty() {
  if (m_renderer) {
    m_renderer->markBaseDirty();
  }
}

void HexViewRhiHost::markSelectionDirty() {
  if (m_renderer) {
    m_renderer->markSelectionDirty();
  }
}

void HexViewRhiHost::invalidateCache() {
  if (m_renderer) {
    m_renderer->invalidateCache();
  }
}

void HexViewRhiHost::requestUpdate() {
  if (m_window) {
    m_window->requestUpdate();
  } else if (m_surface) {
    m_surface->update();
  }
}

void HexViewRhiHost::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_surface) {
    m_surface->setGeometry(rect());
  }
}
