/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollZoomScrollBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

PianoRollZoomScrollBar::PianoRollZoomScrollBar(Qt::Orientation orientation, QWidget* parent)
    : QScrollBar(orientation, parent) {
  setMouseTracking(true);
}

QRect PianoRollZoomScrollBar::zoomOutRect() const {
  if (orientation() == Qt::Horizontal) {
    const int extent = std::min(kButtonExtent, height() - 2);
    const int y = (height() - extent) / 2;
    const int x = width() - (2 * extent) - 2;
    return QRect(std::max(1, x), y, extent, extent);
  }

  const int extent = std::min(kButtonExtent, width() - 2);
  const int x = (width() - extent) / 2;
  const int y = height() - (2 * extent) - 2;
  return QRect(x, std::max(1, y), extent, extent);
}

QRect PianoRollZoomScrollBar::zoomInRect() const {
  if (orientation() == Qt::Horizontal) {
    const QRect out = zoomOutRect();
    return QRect(out.right() + 1, out.top(), out.width(), out.height());
  }

  const QRect out = zoomOutRect();
  return QRect(out.left(), out.bottom() + 1, out.width(), out.height());
}

PianoRollZoomScrollBar::Button PianoRollZoomScrollBar::buttonAt(const QPoint& pos) const {
  if (zoomOutRect().contains(pos)) {
    return Button::ZoomOut;
  }
  if (zoomInRect().contains(pos)) {
    return Button::ZoomIn;
  }
  return Button::None;
}

void PianoRollZoomScrollBar::paintEvent(QPaintEvent* event) {
  QScrollBar::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);

  auto drawButton = [&](const QRect& rect, bool plus, Button button) {
    if (rect.width() <= 2 || rect.height() <= 2) {
      return;
    }

    const QPalette pal = palette();
    QColor fill = pal.color(QPalette::Button);
    if (m_pressed == button) {
      fill = fill.darker(130);
    } else if (m_hovered == button) {
      fill = fill.lighter(118);
    }

    QColor border = pal.color(QPalette::Mid);
    QColor fg = pal.color(QPalette::ButtonText);

    painter.fillRect(rect, fill);
    painter.setPen(border);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    const int cx = rect.center().x();
    const int cy = rect.center().y();
    const int half = std::max(2, std::min(rect.width(), rect.height()) / 4);

    painter.setPen(QPen(fg, 1));
    painter.drawLine(cx - half, cy, cx + half, cy);
    if (plus) {
      painter.drawLine(cx, cy - half, cx, cy + half);
    }
  };

  drawButton(zoomOutRect(), false, Button::ZoomOut);
  drawButton(zoomInRect(), true, Button::ZoomIn);
}

void PianoRollZoomScrollBar::mousePressEvent(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    QScrollBar::mousePressEvent(event);
    return;
  }

  const Button button = buttonAt(event->pos());
  if (button == Button::None) {
    QScrollBar::mousePressEvent(event);
    return;
  }

  m_pressed = button;
  update();

  if (button == Button::ZoomIn) {
    emit zoomInRequested();
  } else if (button == Button::ZoomOut) {
    emit zoomOutRequested();
  }

  event->accept();
}

void PianoRollZoomScrollBar::mouseReleaseEvent(QMouseEvent* event) {
  if (m_pressed == Button::None) {
    QScrollBar::mouseReleaseEvent(event);
    m_hovered = event ? buttonAt(event->pos()) : Button::None;
    update();
    return;
  }

  if (event) {
    event->accept();
  }
  m_pressed = Button::None;
  m_hovered = event ? buttonAt(event->pos()) : Button::None;
  update();
}

void PianoRollZoomScrollBar::mouseMoveEvent(QMouseEvent* event) {
  const Button hovered = event ? buttonAt(event->pos()) : Button::None;
  if (hovered != m_hovered) {
    m_hovered = hovered;
    update();
  }

  if (hovered == Button::None) {
    QScrollBar::mouseMoveEvent(event);
  }
}

void PianoRollZoomScrollBar::leaveEvent(QEvent* event) {
  m_hovered = Button::None;
  update();
  QScrollBar::leaveEvent(event);
}
