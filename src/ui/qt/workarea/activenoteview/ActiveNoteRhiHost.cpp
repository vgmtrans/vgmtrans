/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ActiveNoteRhiHost.h"

#include "ActiveNoteRhiRenderer.h"
#include "ActiveNoteView.h"

#if defined(Q_OS_LINUX)
#include "ActiveNoteRhiWidget.h"
#else
#include "ActiveNoteRhiWindow.h"
#endif

#include <QResizeEvent>
#include <QWindow>

ActiveNoteRhiHost::ActiveNoteRhiHost(ActiveNoteView* view, QWidget* parent)
    : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);

  m_renderer = std::make_unique<ActiveNoteRhiRenderer>(view);

#if defined(Q_OS_LINUX)
  auto* widget = new ActiveNoteRhiWidget(view, m_renderer.get(), this);
  m_surface = widget;
#else
  auto* window = new ActiveNoteRhiWindow(view, m_renderer.get());
  m_window = window;
  m_surface = QWidget::createWindowContainer(window, this);
#endif

  if (m_surface) {
    m_surface->setFocusPolicy(Qt::NoFocus);
    m_surface->setGeometry(rect());
    m_surface->show();
  }
}

ActiveNoteRhiHost::~ActiveNoteRhiHost() {
  delete m_surface;
  m_surface = nullptr;
  m_window = nullptr;
}

void ActiveNoteRhiHost::requestUpdate() {
  if (m_window) {
    m_window->requestUpdate();
  } else if (m_surface) {
    m_surface->update();
  }
}

void ActiveNoteRhiHost::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_surface) {
    m_surface->setGeometry(rect());
  }
}
