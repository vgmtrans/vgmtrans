/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiHost.h"

#include "HexViewRhiTarget.h"

#if defined(Q_OS_LINUX)
#include "HexViewRhiWidget.h"
#else
#include "HexViewRhiWindow.h"
#endif

#include <QResizeEvent>

HexViewRhiHost::HexViewRhiHost(HexView* view, QWidget* parent)
    : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);

#if defined(Q_OS_LINUX)
  auto* widget = new HexViewRhiWidget(view, this);
  m_target = widget;
  m_surface = widget;
#else
  auto* window = new HexViewRhiWindow(view);
  m_target = window;
  m_surface = QWidget::createWindowContainer(window, this);
#endif

  if (m_surface) {
    m_surface->setFocusPolicy(Qt::NoFocus);
    m_surface->setGeometry(rect());
    m_surface->show();
  }
}

void HexViewRhiHost::markBaseDirty() {
  if (m_target) {
    m_target->markBaseDirty();
  }
}

void HexViewRhiHost::markSelectionDirty() {
  if (m_target) {
    m_target->markSelectionDirty();
  }
}

void HexViewRhiHost::invalidateCache() {
  if (m_target) {
    m_target->invalidateCache();
  }
}

void HexViewRhiHost::requestUpdate() {
  if (m_target) {
    m_target->requestUpdate();
  }
}

void HexViewRhiHost::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_surface) {
    m_surface->setGeometry(rect());
  }
}
