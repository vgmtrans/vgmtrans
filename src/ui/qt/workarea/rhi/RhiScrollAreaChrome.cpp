/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RhiScrollAreaChrome.h"

#include "RhiScrollBar.h"

#include <QAbstractScrollArea>
#include <QPalette>
#include <QScrollBar>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {
RhiScrollButtonSnapshot makeButtonSnapshot(const QRect& rect,
                                          RhiScrollButtonGlyph glyph,
                                          bool hovered,
                                          bool pressed) {
  return RhiScrollButtonSnapshot{
      QRectF(rect),
      glyph,
      hovered,
      pressed,
      !rect.isEmpty(),
  };
}
}  // namespace

RhiScrollAreaChrome::RhiScrollAreaChrome(QAbstractScrollArea* area,
                                         ViewportMarginsApplier applyViewportMargins,
                                         RedrawCallback requestRedraw)
    : m_area(area),
      m_applyViewportMargins(std::move(applyViewportMargins)),
      m_requestRedraw(std::move(requestRedraw)) {
  if (!m_area) {
    return;
  }

  auto redraw = [this]() {
    updateSnapshot();
    this->requestRedraw();
  };
  m_horizontalBar = std::make_unique<RhiScrollBar>(Qt::Horizontal, redraw, m_area);
  m_verticalBar = std::make_unique<RhiScrollBar>(Qt::Vertical, redraw, m_area);

  QScrollBar* hbar = m_area->horizontalScrollBar();
  QScrollBar* vbar = m_area->verticalScrollBar();
  m_horizontalBar->bindTo(hbar);
  m_verticalBar->bindTo(vbar);

  if (hbar) {
    m_horizontalRangeConnection = QObject::connect(hbar, &QScrollBar::rangeChanged, m_area, [this]() {
      syncLayout();
    });
  }
  if (vbar) {
    m_verticalRangeConnection = QObject::connect(vbar, &QScrollBar::rangeChanged, m_area, [this]() {
      syncLayout();
    });
  }

  // Keep QAbstractScrollArea's built-in scrollbars as hidden models only.
  m_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

RhiScrollAreaChrome::~RhiScrollAreaChrome() = default;

void RhiScrollAreaChrome::setHorizontalPolicy(Qt::ScrollBarPolicy policy) {
  if (m_horizontalPolicy == policy) {
    return;
  }
  m_horizontalPolicy = policy;
  syncLayout();
}

void RhiScrollAreaChrome::setVerticalPolicy(Qt::ScrollBarPolicy policy) {
  if (m_verticalPolicy == policy) {
    return;
  }
  m_verticalPolicy = policy;
  syncLayout();
}

void RhiScrollAreaChrome::setHorizontalArrowButtonsVisible(bool visible) {
  if (!m_horizontalBar) {
    return;
  }
  m_horizontalBar->setArrowButtonsVisible(visible);
  syncLayout();
}

void RhiScrollAreaChrome::setVerticalArrowButtonsVisible(bool visible) {
  if (!m_verticalBar) {
    return;
  }
  m_verticalBar->setArrowButtonsVisible(visible);
  syncLayout();
}

void RhiScrollAreaChrome::setHorizontalButtons(std::vector<ButtonSpec> buttons) {
  m_horizontalButtons = std::move(buttons);
  m_hoveredHorizontalButton = -1;
  m_pressedHorizontalButton = -1;
  syncLayout();
}

void RhiScrollAreaChrome::setVerticalButtons(std::vector<ButtonSpec> buttons) {
  m_verticalButtons = std::move(buttons);
  m_hoveredVerticalButton = -1;
  m_pressedVerticalButton = -1;
  syncLayout();
}

bool RhiScrollAreaChrome::shouldShowHorizontalBar() const {
  QScrollBar* hbar = m_area ? m_area->horizontalScrollBar() : nullptr;
  if (!hbar) {
    return false;
  }
  if (m_horizontalPolicy == Qt::ScrollBarAlwaysOn) {
    return true;
  }
  if (m_horizontalPolicy == Qt::ScrollBarAlwaysOff) {
    return false;
  }
  return hbar->maximum() > hbar->minimum();
}

bool RhiScrollAreaChrome::shouldShowVerticalBar() const {
  QScrollBar* vbar = m_area ? m_area->verticalScrollBar() : nullptr;
  if (!vbar) {
    return false;
  }
  if (m_verticalPolicy == Qt::ScrollBarAlwaysOn) {
    return true;
  }
  if (m_verticalPolicy == Qt::ScrollBarAlwaysOff) {
    return false;
  }
  return vbar->maximum() > vbar->minimum();
}

QRect RhiScrollAreaChrome::horizontalButtonsRect() const {
  if (!m_area || m_horizontalButtons.empty() || m_snapshot.bottomMargin <= 0) {
    return {};
  }
  const QRect viewportRect = m_area->viewport()->geometry();
  const int extent = m_snapshot.bottomMargin;
  const int width = extent * static_cast<int>(m_horizontalButtons.size());
  return QRect(viewportRect.right() - width + 1,
               viewportRect.bottom() + 1,
               width,
               extent);
}

QRect RhiScrollAreaChrome::verticalButtonsRect() const {
  if (!m_area || m_verticalButtons.empty() || m_snapshot.rightMargin <= 0) {
    return {};
  }
  const QRect viewportRect = m_area->viewport()->geometry();
  const int extent = m_snapshot.rightMargin;
  const int height = extent * static_cast<int>(m_verticalButtons.size());
  return QRect(viewportRect.right() + 1,
               viewportRect.bottom() - height + 1,
               extent,
               height);
}

QRect RhiScrollAreaChrome::buttonRect(Qt::Orientation orientation, int index) const {
  if (index < 0) {
    return {};
  }

  if (orientation == Qt::Horizontal) {
    const QRect strip = horizontalButtonsRect();
    const int extent = m_snapshot.bottomMargin;
    if (strip.isEmpty() || extent <= 0) {
      return {};
    }
    return QRect(strip.left() + (index * extent), strip.top(), extent, strip.height());
  }

  const QRect strip = verticalButtonsRect();
  const int extent = m_snapshot.rightMargin;
  if (strip.isEmpty() || extent <= 0) {
    return {};
  }
  return QRect(strip.left(), strip.top() + (index * extent), strip.width(), extent);
}

int RhiScrollAreaChrome::buttonIndexAt(Qt::Orientation orientation, const QPoint& pos) const {
  const std::vector<ButtonSpec>& buttons = (orientation == Qt::Horizontal) ? m_horizontalButtons : m_verticalButtons;
  for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
    if (buttonRect(orientation, i).contains(pos)) {
      return i;
    }
  }
  return -1;
}

void RhiScrollAreaChrome::syncLayout() {
  if (!m_area || !m_horizontalBar || !m_verticalBar) {
    return;
  }

  const bool showHorizontal = shouldShowHorizontalBar();
  const bool showVertical = shouldShowVerticalBar();
  const int horizontalExtent = showHorizontal ? m_horizontalBar->sizeHint().height() : 0;
  const int verticalExtent = showVertical ? m_verticalBar->sizeHint().width() : 0;

  if (m_applyViewportMargins) {
    m_applyViewportMargins(QMargins(0, 0, verticalExtent, horizontalExtent));
  }

  const QRect viewportRect = m_area->viewport()->geometry();
  // The hidden viewport still defines the content rect. The chrome lives in
  // the margin lanes around it and is rendered by the RHI renderer.
  const QRect horizontalLane(viewportRect.left(),
                             viewportRect.bottom() + 1,
                             viewportRect.width(),
                             horizontalExtent);
  const QRect verticalLane(viewportRect.right() + 1,
                           viewportRect.top(),
                           verticalExtent,
                           viewportRect.height());

  int horizontalButtonStripWidth = 0;
  if (showHorizontal && horizontalExtent > 0) {
    horizontalButtonStripWidth = std::min(horizontalLane.width(),
                                          horizontalExtent * static_cast<int>(m_horizontalButtons.size()));
  }
  int verticalButtonStripHeight = 0;
  if (showVertical && verticalExtent > 0) {
    verticalButtonStripHeight = std::min(verticalLane.height(),
                                         verticalExtent * static_cast<int>(m_verticalButtons.size()));
  }

  m_horizontalBar->setVisible(showHorizontal);
  m_verticalBar->setVisible(showVertical);
  m_horizontalBar->setGeometry(showHorizontal
                                   ? QRect(horizontalLane.left(),
                                           horizontalLane.top(),
                                           std::max(0, horizontalLane.width() - horizontalButtonStripWidth),
                                           horizontalLane.height())
                                   : QRect());
  m_verticalBar->setGeometry(showVertical
                                 ? QRect(verticalLane.left(),
                                         verticalLane.top(),
                                         verticalLane.width(),
                                         std::max(0, verticalLane.height() - verticalButtonStripHeight))
                                 : QRect());

  m_hoveredHorizontalButton = (showHorizontal ? m_hoveredHorizontalButton : -1);
  m_pressedHorizontalButton = (showHorizontal ? m_pressedHorizontalButton : -1);
  m_hoveredVerticalButton = (showVertical ? m_hoveredVerticalButton : -1);
  m_pressedVerticalButton = (showVertical ? m_pressedVerticalButton : -1);

  updateSnapshot();
  requestRedraw();
}

void RhiScrollAreaChrome::updateSnapshot() {
  m_snapshot = {};
  if (!m_area || !m_horizontalBar || !m_verticalBar) {
    return;
  }

  m_snapshot.bottomMargin = m_horizontalBar->snapshot().visible ? m_horizontalBar->sizeHint().height() : 0;
  m_snapshot.rightMargin = m_verticalBar->snapshot().visible ? m_verticalBar->sizeHint().width() : 0;
  m_snapshot.colors.laneColor = RhiScrollBar::laneColorForPalette(m_area->palette());
  m_snapshot.colors.borderColor = RhiScrollBar::borderColorForPalette(m_area->palette());
  m_snapshot.colors.thumbColor = RhiScrollBar::thumbColorForPalette(m_area->palette());
  m_snapshot.colors.thumbHoverColor = RhiScrollBar::thumbHoverColorForPalette(m_area->palette());
  m_snapshot.colors.thumbPressedColor = RhiScrollBar::thumbPressedColorForPalette(m_area->palette());
  m_snapshot.colors.buttonHoverColor = RhiScrollBar::buttonHoverColorForPalette(m_area->palette());
  m_snapshot.colors.buttonPressedColor = RhiScrollBar::buttonPressedColorForPalette(m_area->palette());
  m_snapshot.colors.glyphColor = RhiScrollBar::glyphColorForPalette(m_area->palette());
  m_snapshot.horizontal = m_horizontalBar->snapshot();
  m_snapshot.vertical = m_verticalBar->snapshot();

  const QRect viewportRect = m_area->viewport()->geometry();
  if (m_snapshot.horizontal.visible && m_snapshot.vertical.visible) {
    m_snapshot.cornerRect = QRectF(viewportRect.right() + 1,
                                   viewportRect.bottom() + 1,
                                   m_snapshot.rightMargin,
                                   m_snapshot.bottomMargin);
  }

  for (int i = 0; i < static_cast<int>(m_horizontalButtons.size()); ++i) {
    const QRect rect = buttonRect(Qt::Horizontal, i);
    m_snapshot.horizontalButtons.push_back(makeButtonSnapshot(
        rect,
        m_horizontalButtons[static_cast<size_t>(i)].glyph,
        m_hoveredHorizontalButton == i,
        m_pressedHorizontalButton == i));
  }

  for (int i = 0; i < static_cast<int>(m_verticalButtons.size()); ++i) {
    const QRect rect = buttonRect(Qt::Vertical, i);
    m_snapshot.verticalButtons.push_back(makeButtonSnapshot(
        rect,
        m_verticalButtons[static_cast<size_t>(i)].glyph,
        m_hoveredVerticalButton == i,
        m_pressedVerticalButton == i));
  }
}

void RhiScrollAreaChrome::requestRedraw() const {
  if (m_requestRedraw) {
    m_requestRedraw();
  }
}

bool RhiScrollAreaChrome::handleMousePress(const QPoint& pos) {
  if (!m_area) {
    return false;
  }

  const int horizontalButton = buttonIndexAt(Qt::Horizontal, pos);
  if (horizontalButton >= 0) {
    m_pressedHorizontalButton = horizontalButton;
    m_hoveredHorizontalButton = horizontalButton;
    updateSnapshot();
    requestRedraw();
    return true;
  }

  const int verticalButton = buttonIndexAt(Qt::Vertical, pos);
  if (verticalButton >= 0) {
    m_pressedVerticalButton = verticalButton;
    m_hoveredVerticalButton = verticalButton;
    updateSnapshot();
    requestRedraw();
    return true;
  }

  if (m_horizontalBar && m_horizontalBar->handleMousePress(pos)) {
    return true;
  }
  if (m_verticalBar && m_verticalBar->handleMousePress(pos)) {
    return true;
  }
  return false;
}

bool RhiScrollAreaChrome::handleMouseMove(const QPoint& pos, Qt::MouseButtons buttons) {
  if (!m_area) {
    return false;
  }

  const int nextHoveredHorizontal = buttonIndexAt(Qt::Horizontal, pos);
  const int nextHoveredVertical = buttonIndexAt(Qt::Vertical, pos);
  bool handled = false;

  if (nextHoveredHorizontal != m_hoveredHorizontalButton) {
    m_hoveredHorizontalButton = nextHoveredHorizontal;
    handled = true;
  }
  if (nextHoveredVertical != m_hoveredVerticalButton) {
    m_hoveredVerticalButton = nextHoveredVertical;
    handled = true;
  }
  if (handled) {
    updateSnapshot();
    requestRedraw();
  }

  bool barHandled = false;
  if (m_horizontalBar && m_horizontalBar->handleMouseMove(pos, buttons)) {
    barHandled = true;
  }
  if (m_verticalBar && m_verticalBar->handleMouseMove(pos, buttons)) {
    barHandled = true;
  }
  return handled || barHandled;
}

bool RhiScrollAreaChrome::handleMouseRelease(const QPoint& pos) {
  if (!m_area) {
    return false;
  }

  bool handled = false;
  if (m_pressedHorizontalButton >= 0) {
    const int pressed = m_pressedHorizontalButton;
    m_pressedHorizontalButton = -1;
    if (pressed == buttonIndexAt(Qt::Horizontal, pos)) {
      const auto& button = m_horizontalButtons[static_cast<size_t>(pressed)];
      if (button.onPressed) {
        button.onPressed();
      }
      handled = true;
    }
  }
  if (m_pressedVerticalButton >= 0) {
    const int pressed = m_pressedVerticalButton;
    m_pressedVerticalButton = -1;
    if (pressed == buttonIndexAt(Qt::Vertical, pos)) {
      const auto& button = m_verticalButtons[static_cast<size_t>(pressed)];
      if (button.onPressed) {
        button.onPressed();
      }
      handled = true;
    }
  }

  if (m_horizontalBar && m_horizontalBar->handleMouseRelease(pos)) {
    handled = true;
  }
  if (m_verticalBar && m_verticalBar->handleMouseRelease(pos)) {
    handled = true;
  }

  updateSnapshot();
  requestRedraw();
  return handled;
}

void RhiScrollAreaChrome::handleLeave() {
  m_hoveredHorizontalButton = -1;
  m_pressedHorizontalButton = -1;
  m_hoveredVerticalButton = -1;
  m_pressedVerticalButton = -1;
  if (m_horizontalBar) {
    m_horizontalBar->handleLeave();
  }
  if (m_verticalBar) {
    m_verticalBar->handleLeave();
  }
  updateSnapshot();
  requestRedraw();
}
