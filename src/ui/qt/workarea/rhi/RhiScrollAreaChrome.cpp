/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RhiScrollAreaChrome.h"

#include "RhiScrollBar.h"

#include <QAbstractScrollArea>
#include <QPainter>
#include <QScrollBar>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {
class ScrollCornerFill final : public QWidget {
public:
  explicit ScrollCornerFill(QWidget* parent = nullptr)
      : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), RhiScrollBar::laneColorForPalette(palette()));
  }
};
}  // namespace

RhiScrollAreaChrome::RhiScrollAreaChrome(QAbstractScrollArea* area,
                                         ViewportMarginsApplier applyViewportMargins)
    : m_area(area),
      m_applyViewportMargins(std::move(applyViewportMargins)) {
  if (!m_area) {
    return;
  }

  m_horizontalBar = new RhiScrollBar(Qt::Horizontal, m_area);
  m_verticalBar = new RhiScrollBar(Qt::Vertical, m_area);
  m_cornerFill = new ScrollCornerFill(m_area);

  m_horizontalBar->bindTo(m_area->horizontalScrollBar());
  m_verticalBar->bindTo(m_area->verticalScrollBar());

  // Keep QAbstractScrollArea's scroll models, but suppress the platform bars.
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

void RhiScrollAreaChrome::setHorizontalTrailingWidget(QWidget* widget) {
  if (m_horizontalTrailingWidget == widget) {
    return;
  }
  m_horizontalTrailingWidget = widget;
  if (m_horizontalTrailingWidget) {
    m_horizontalTrailingWidget->setParent(m_area);
    m_horizontalTrailingWidget->show();
  }
  syncLayout();
}

void RhiScrollAreaChrome::setVerticalTrailingWidget(QWidget* widget) {
  if (m_verticalTrailingWidget == widget) {
    return;
  }
  m_verticalTrailingWidget = widget;
  if (m_verticalTrailingWidget) {
    m_verticalTrailingWidget->setParent(m_area);
    m_verticalTrailingWidget->show();
  }
  syncLayout();
}

bool RhiScrollAreaChrome::shouldShowHorizontalBar() const {
  if (!m_area || !m_area->horizontalScrollBar()) {
    return false;
  }
  if (m_horizontalPolicy == Qt::ScrollBarAlwaysOn) {
    return true;
  }
  if (m_horizontalPolicy == Qt::ScrollBarAlwaysOff) {
    return false;
  }
  return m_area->horizontalScrollBar()->maximum() > m_area->horizontalScrollBar()->minimum();
}

bool RhiScrollAreaChrome::shouldShowVerticalBar() const {
  if (!m_area || !m_area->verticalScrollBar()) {
    return false;
  }
  if (m_verticalPolicy == Qt::ScrollBarAlwaysOn) {
    return true;
  }
  if (m_verticalPolicy == Qt::ScrollBarAlwaysOff) {
    return false;
  }
  return m_area->verticalScrollBar()->maximum() > m_area->verticalScrollBar()->minimum();
}

void RhiScrollAreaChrome::syncLayout() {
  if (!m_area || !m_horizontalBar || !m_verticalBar) {
    return;
  }

  const bool showHorizontal = shouldShowHorizontalBar();
  const bool showVertical = shouldShowVerticalBar();
  const int horizontalExtent = showHorizontal
      ? std::max(m_horizontalBar->sizeHint().height(),
                 m_horizontalTrailingWidget ? m_horizontalTrailingWidget->sizeHint().height() : 0)
      : 0;
  const int verticalExtent = showVertical
      ? std::max(m_verticalBar->sizeHint().width(),
                 m_verticalTrailingWidget ? m_verticalTrailingWidget->sizeHint().width() : 0)
      : 0;

  if (m_applyViewportMargins) {
    m_applyViewportMargins(QMargins(0, 0, verticalExtent, horizontalExtent));
  }

  const QRect viewportRect = m_area->viewport()->geometry();
  const QRect horizontalLane(viewportRect.left(),
                             viewportRect.bottom() + 1,
                             viewportRect.width(),
                             horizontalExtent);
  const QRect verticalLane(viewportRect.right() + 1,
                           viewportRect.top(),
                           verticalExtent,
                           viewportRect.height());

  if (showHorizontal) {
    int trailingWidth = 0;
    if (m_horizontalTrailingWidget) {
      trailingWidth = std::clamp(m_horizontalTrailingWidget->sizeHint().width(), 0, horizontalLane.width());
      m_horizontalTrailingWidget->setGeometry(horizontalLane.right() - trailingWidth + 1,
                                             horizontalLane.top(),
                                             trailingWidth,
                                             horizontalLane.height());
      m_horizontalTrailingWidget->show();
    }
    m_horizontalBar->setGeometry(horizontalLane.left(),
                                 horizontalLane.top(),
                                 std::max(0, horizontalLane.width() - trailingWidth),
                                 horizontalLane.height());
    m_horizontalBar->show();
    m_horizontalBar->syncFromModel();
  } else {
    m_horizontalBar->hide();
    if (m_horizontalTrailingWidget) {
      m_horizontalTrailingWidget->hide();
    }
  }

  if (showVertical) {
    int trailingHeight = 0;
    if (m_verticalTrailingWidget) {
      trailingHeight = std::clamp(m_verticalTrailingWidget->sizeHint().height(), 0, verticalLane.height());
      m_verticalTrailingWidget->setGeometry(verticalLane.left(),
                                           verticalLane.bottom() - trailingHeight + 1,
                                           verticalLane.width(),
                                           trailingHeight);
      m_verticalTrailingWidget->show();
    }
    m_verticalBar->setGeometry(verticalLane.left(),
                               verticalLane.top(),
                               verticalLane.width(),
                               std::max(0, verticalLane.height() - trailingHeight));
    m_verticalBar->show();
    m_verticalBar->syncFromModel();
  } else {
    m_verticalBar->hide();
    if (m_verticalTrailingWidget) {
      m_verticalTrailingWidget->hide();
    }
  }

  if (showHorizontal && showVertical && m_cornerFill) {
    m_cornerFill->setGeometry(viewportRect.right() + 1,
                              viewportRect.bottom() + 1,
                              verticalExtent,
                              horizontalExtent);
    m_cornerFill->show();
    m_cornerFill->update();
  } else if (m_cornerFill) {
    m_cornerFill->hide();
  }
}
