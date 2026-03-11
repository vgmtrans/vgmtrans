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

#include <algorithm>
#include <utility>

namespace {
constexpr Qt::Orientation kOrientations[] = {Qt::Horizontal, Qt::Vertical};

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
    QObject::connect(hbar, &QScrollBar::rangeChanged, m_area, [this]() {
      syncLayout();
    });
  }
  if (vbar) {
    QObject::connect(vbar, &QScrollBar::rangeChanged, m_area, [this]() {
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
  setButtons(Qt::Horizontal, std::move(buttons));
}

void RhiScrollAreaChrome::setVerticalButtons(std::vector<ButtonSpec> buttons) {
  setButtons(Qt::Vertical, std::move(buttons));
}

void RhiScrollAreaChrome::setButtons(Qt::Orientation orientation, std::vector<ButtonSpec> buttonSpecs) {
  buttons(orientation) = std::move(buttonSpecs);
  hoveredButton(orientation) = -1;
  pressedButton(orientation) = -1;
  syncLayout();
}

const std::vector<RhiScrollAreaChrome::ButtonSpec>& RhiScrollAreaChrome::buttons(Qt::Orientation orientation) const {
  return (orientation == Qt::Horizontal) ? m_horizontalButtons : m_verticalButtons;
}

std::vector<RhiScrollAreaChrome::ButtonSpec>& RhiScrollAreaChrome::buttons(Qt::Orientation orientation) {
  return (orientation == Qt::Horizontal) ? m_horizontalButtons : m_verticalButtons;
}

int& RhiScrollAreaChrome::hoveredButton(Qt::Orientation orientation) {
  return (orientation == Qt::Horizontal) ? m_hoveredHorizontalButton : m_hoveredVerticalButton;
}

int& RhiScrollAreaChrome::pressedButton(Qt::Orientation orientation) {
  return (orientation == Qt::Horizontal) ? m_pressedHorizontalButton : m_pressedVerticalButton;
}

int RhiScrollAreaChrome::laneExtent(Qt::Orientation orientation) const {
  return (orientation == Qt::Horizontal) ? m_snapshot.bottomMargin : m_snapshot.rightMargin;
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

QRect RhiScrollAreaChrome::buttonsRect(Qt::Orientation orientation) const {
  if (!m_area || buttons(orientation).empty()) {
    return {};
  }

  const QRect viewportRect = m_area->viewport()->geometry();
  const int extent = laneExtent(orientation);
  if (extent <= 0) {
    return {};
  }

  if (orientation == Qt::Horizontal) {
    const int width = extent * static_cast<int>(m_horizontalButtons.size());
    return QRect(viewportRect.right() - width + 1,
                 viewportRect.bottom() + 1,
                 width,
                 extent);
  }

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

  const QRect strip = buttonsRect(orientation);
  const int extent = laneExtent(orientation);
  if (strip.isEmpty() || extent <= 0) {
    return {};
  }

  if (orientation == Qt::Horizontal) {
    return QRect(strip.left() + (index * extent), strip.top(), extent, strip.height());
  }
  return QRect(strip.left(), strip.top() + (index * extent), strip.width(), extent);
}

int RhiScrollAreaChrome::buttonIndexAt(Qt::Orientation orientation, const QPoint& pos) const {
  const auto& orientationButtons = buttons(orientation);
  for (int i = 0; i < static_cast<int>(orientationButtons.size()); ++i) {
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

  // The hidden QScrollBars keep their full range math. The visible chrome just
  // borrows margin lanes around the viewport and mirrors that model state.
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

  hoveredButton(Qt::Horizontal) = showHorizontal ? hoveredButton(Qt::Horizontal) : -1;
  pressedButton(Qt::Horizontal) = showHorizontal ? pressedButton(Qt::Horizontal) : -1;
  hoveredButton(Qt::Vertical) = showVertical ? hoveredButton(Qt::Vertical) : -1;
  pressedButton(Qt::Vertical) = showVertical ? pressedButton(Qt::Vertical) : -1;

  refresh();
}

void RhiScrollAreaChrome::refresh() {
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

  appendButtonSnapshots(Qt::Horizontal);
  appendButtonSnapshots(Qt::Vertical);
}

void RhiScrollAreaChrome::appendButtonSnapshots(Qt::Orientation orientation) {
  // Custom edge buttons share the same lane/palette state as the scrollbar and
  // are emitted into the same renderer-facing snapshot.
  auto& destination = (orientation == Qt::Horizontal)
      ? m_snapshot.horizontalButtons
      : m_snapshot.verticalButtons;
  const auto& orientationButtons = buttons(orientation);
  const int hoveredIndex = hoveredButton(orientation);
  const int pressedIndex = pressedButton(orientation);

  for (int i = 0; i < static_cast<int>(orientationButtons.size()); ++i) {
    destination.push_back(makeButtonSnapshot(
        buttonRect(orientation, i),
        orientationButtons[static_cast<size_t>(i)].glyph,
        hoveredIndex == i,
        pressedIndex == i));
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

  for (Qt::Orientation orientation : kOrientations) {
    const int index = buttonIndexAt(orientation, pos);
    if (index >= 0) {
      pressedButton(orientation) = index;
      hoveredButton(orientation) = index;
      refresh();
      return true;
    }
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

  bool buttonHandled = false;
  for (Qt::Orientation orientation : kOrientations) {
    const int nextHovered = buttonIndexAt(orientation, pos);
    if (nextHovered != hoveredButton(orientation)) {
      hoveredButton(orientation) = nextHovered;
      buttonHandled = true;
    }
  }
  if (buttonHandled) {
    refresh();
  }

  bool barHandled = false;
  if (m_horizontalBar && m_horizontalBar->handleMouseMove(pos, buttons)) {
    barHandled = true;
  }
  if (m_verticalBar && m_verticalBar->handleMouseMove(pos, buttons)) {
    barHandled = true;
  }
  return buttonHandled || barHandled;
}

bool RhiScrollAreaChrome::handleMouseRelease(const QPoint& pos) {
  if (!m_area) {
    return false;
  }

  bool handled = false;
  for (Qt::Orientation orientation : kOrientations) {
    const int pressedIndex = pressedButton(orientation);
    if (pressedIndex < 0) {
      continue;
    }

    pressedButton(orientation) = -1;
    if (pressedIndex == buttonIndexAt(orientation, pos)) {
      const auto& button = buttons(orientation)[static_cast<size_t>(pressedIndex)];
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

  refresh();
  return handled;
}

void RhiScrollAreaChrome::handleLeave() {
  hoveredButton(Qt::Horizontal) = -1;
  pressedButton(Qt::Horizontal) = -1;
  hoveredButton(Qt::Vertical) = -1;
  pressedButton(Qt::Vertical) = -1;
  if (m_horizontalBar) {
    m_horizontalBar->handleLeave();
  }
  if (m_verticalBar) {
    m_verticalBar->handleLeave();
  }
  refresh();
}
