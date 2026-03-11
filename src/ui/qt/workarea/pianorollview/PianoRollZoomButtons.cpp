/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PianoRollZoomButtons.h"

#include "workarea/rhi/RhiScrollBar.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace {
QColor buttonFill(const QPalette& palette, bool hovered, bool pressed) {
  QColor color = RhiScrollBar::laneColorForPalette(palette);
  if (pressed) {
    return color.darker(105);
  }
  if (hovered) {
    return color.lighter(102);
  }
  return color;
}
}  // namespace

PianoRollZoomButtons::PianoRollZoomButtons(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent),
      m_orientation(orientation) {
  setMouseTracking(true);
}

QSize PianoRollZoomButtons::sizeHint() const {
  return (m_orientation == Qt::Horizontal)
      ? QSize((2 * kButtonExtent) + 1, kButtonExtent)
      : QSize(kButtonExtent, (2 * kButtonExtent) + 1);
}

QSize PianoRollZoomButtons::minimumSizeHint() const {
  return sizeHint();
}

QRect PianoRollZoomButtons::zoomOutRect() const {
  if (m_orientation == Qt::Horizontal) {
    return QRect(0, 0, std::min(kButtonExtent, width()), height());
  }
  return QRect(0, 0, width(), std::min(kButtonExtent, height()));
}

QRect PianoRollZoomButtons::zoomInRect() const {
  if (m_orientation == Qt::Horizontal) {
    const QRect out = zoomOutRect();
    return QRect(out.right() + 1, 0, std::min(kButtonExtent, std::max(0, width() - out.width())), height());
  }
  const QRect out = zoomOutRect();
  return QRect(0, out.bottom() + 1, width(), std::min(kButtonExtent, std::max(0, height() - out.height())));
}

PianoRollZoomButtons::Button PianoRollZoomButtons::buttonAt(const QPoint& pos) const {
  if (zoomOutRect().contains(pos)) {
    return Button::ZoomOut;
  }
  if (zoomInRect().contains(pos)) {
    return Button::ZoomIn;
  }
  return Button::None;
}

void PianoRollZoomButtons::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  painter.fillRect(rect(), RhiScrollBar::laneColorForPalette(palette()));

  const QColor border = RhiScrollBar::borderColorForPalette(palette());
  const QColor glyph = RhiScrollBar::glyphColorForPalette(palette());

  auto drawButton = [&](const QRect& rect, bool plus, Button button) {
    if (rect.isEmpty()) {
      return;
    }

    painter.fillRect(rect, buttonFill(palette(), m_hovered == button, m_pressed == button));
    painter.setPen(border);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    const int cx = rect.center().x();
    const int cy = rect.center().y();
    const int half = std::max(2, std::min(rect.width(), rect.height()) / 4);
    painter.setPen(QPen(glyph, 1));
    painter.drawLine(cx - half, cy, cx + half, cy);
    if (plus) {
      painter.drawLine(cx, cy - half, cx, cy + half);
    }
  };

  drawButton(zoomOutRect(), false, Button::ZoomOut);
  drawButton(zoomInRect(), true, Button::ZoomIn);
}

void PianoRollZoomButtons::mousePressEvent(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  m_pressed = buttonAt(event->pos());
  if (m_pressed == Button::ZoomIn) {
    emit zoomInRequested();
  } else if (m_pressed == Button::ZoomOut) {
    emit zoomOutRequested();
  }
  update();
  event->accept();
}

void PianoRollZoomButtons::mouseReleaseEvent(QMouseEvent* event) {
  if (!event) {
    QWidget::mouseReleaseEvent(event);
    return;
  }

  m_pressed = Button::None;
  m_hovered = buttonAt(event->pos());
  update();
  event->accept();
}

void PianoRollZoomButtons::mouseMoveEvent(QMouseEvent* event) {
  const Button hovered = event ? buttonAt(event->pos()) : Button::None;
  if (hovered != m_hovered) {
    m_hovered = hovered;
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void PianoRollZoomButtons::leaveEvent(QEvent* event) {
  m_hovered = Button::None;
  update();
  QWidget::leaveEvent(event);
}
