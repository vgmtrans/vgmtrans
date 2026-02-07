/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexViewRhiHost.h"

#include "HexView.h"
#include "HexViewRhiRenderer.h"
#include "HexViewRhiWidget.h"

#include <QResizeEvent>

HexViewRhiHost::HexViewRhiHost(HexView* view, QWidget* parent)
    : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);

  m_renderer = std::make_unique<HexViewRhiRenderer>(view, "HexViewRhiWidget");
  auto* widget = new HexViewRhiWidget(view, m_renderer.get(), this);
  m_surface = widget;

  if (m_surface) {
    m_surface->setFocusPolicy(Qt::NoFocus);
    m_surface->setGeometry(rect());
    m_surface->show();
  }
}

HexViewRhiHost::~HexViewRhiHost() {
  delete m_surface;
  m_surface = nullptr;
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
  if (m_surface) {
    m_surface->update();
  }
}

void HexViewRhiHost::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_surface) {
    m_surface->setGeometry(rect());
  }
}
