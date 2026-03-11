/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SeekBar.h"

#include <cmath>

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace {
constexpr qreal TRACK_THICKNESS = 4.0;
constexpr qreal THUMB_RADIUS = 10.0;
constexpr qreal TRACK_RADIUS = TRACK_THICKNESS * 0.5;
constexpr int DIRTY_PADDING = 2;

QColor trackColorFor(bool enabled) {
  return enabled ? QColor(66, 66, 66) : QColor(73, 73, 73);
}

QColor fillColorFor(bool enabled) {
  return enabled ? QColor(100, 100, 100) : QColor(116, 116, 116);
}

QColor thumbColorFor(bool enabled) {
  return enabled ? QColor(149, 149, 149) : QColor(160, 160, 160);
}
}

SeekBar::SeekBar(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent), m_orientation(orientation) {
  setSizePolicy(m_orientation == Qt::Horizontal ? QSizePolicy::Expanding : QSizePolicy::Fixed,
                m_orientation == Qt::Horizontal ? QSizePolicy::Fixed : QSizePolicy::Expanding);
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_NoSystemBackground);
}

void SeekBar::setRange(int minimum, int maximum) {
  // Keep range normalization local so callers can pass song lengths directly.
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  if (m_minimum == minimum && m_maximum == maximum) {
    return;
  }

  m_minimum = minimum;
  m_maximum = std::max(minimum, maximum);
  m_value = std::clamp(m_value, m_minimum, m_maximum);
  update();
}

void SeekBar::setValue(int value) {
  // Passive playback updates only repaint when the thumb crosses a device pixel.
  const int clamped = std::clamp(value, m_minimum, m_maximum);
  if (m_value == clamped) {
    return;
  }

  const int oldValue = m_value;
  m_value = clamped;
  if (displayedThumbPixel(oldValue) == displayedThumbPixel(m_value)) {
    return;
  }
  update(dirtyRectForValues(oldValue, m_value));
}

QSize SeekBar::sizeHint() const {
  if (m_orientation == Qt::Horizontal) {
    return QSize(160, 26);
  }
  return QSize(26, 160);
}

void SeekBar::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  switch (event->type()) {
    case QEvent::EnabledChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
      update();
      break;
    default:
      break;
  }
}

void SeekBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mousePressEvent(event);
    return;
  }

  m_sliderDown = true;
  updateValueFromPointer(event->position());
  grabMouse();
  event->accept();
}

void SeekBar::mouseMoveEvent(QMouseEvent* event) {
  if (!m_sliderDown || !isEnabled()) {
    QWidget::mouseMoveEvent(event);
    return;
  }

  updateValueFromPointer(event->position());
  event->accept();
}

void SeekBar::mouseReleaseEvent(QMouseEvent* event) {
  if (!m_sliderDown || event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }

  updateValueFromPointer(event->position());
  m_sliderDown = false;
  releaseMouse();
  emit sliderReleased();
  event->accept();
}

void SeekBar::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setClipRect(event->rect());

  const QRectF track = trackRect();
  const QColor trackColor = trackColorFor(isEnabled());
  const QColor fillColor = fillColorFor(isEnabled());
  const QColor thumbColor = thumbColorFor(isEnabled());

  painter.setPen(Qt::NoPen);
  painter.setBrush(trackColor);
  painter.drawRoundedRect(track, TRACK_RADIUS, TRACK_RADIUS);

  if (m_maximum > m_minimum) {
    const qreal thumbCenter = thumbCenterForValue(m_value);
    QRectF played = track;
    if (m_orientation == Qt::Horizontal) {
      played.setRight(std::max(track.left(), thumbCenter));
    } else {
      played.setTop(std::min(track.bottom(), thumbCenter));
    }
    if (played.width() > 0.0 && played.height() > 0.0) {
      painter.setBrush(fillColor);
      painter.drawRoundedRect(played, TRACK_RADIUS, TRACK_RADIUS);
    }
  }

  painter.setBrush(thumbColor);
  if (m_orientation == Qt::Horizontal) {
    const qreal centerX = thumbCenterForValue(m_value);
    painter.drawEllipse(QPointF(centerX, rect().center().y()), THUMB_RADIUS, THUMB_RADIUS);
  } else {
    const qreal centerY = thumbCenterForValue(m_value);
    painter.drawEllipse(QPointF(rect().center().x(), centerY), THUMB_RADIUS, THUMB_RADIUS);
  }
}

QRectF SeekBar::trackRect() const {
  // Reserve thumb radius at both ends so the handle stays fully inside the widget.
  if (m_orientation == Qt::Horizontal) {
    const qreal left = THUMB_RADIUS;
    const qreal width = std::max<qreal>(0.0, rect().width() - THUMB_RADIUS * 2.0);
    return QRectF(left, (rect().height() - TRACK_THICKNESS) * 0.5, width, TRACK_THICKNESS);
  }

  const qreal top = THUMB_RADIUS;
  const qreal height = std::max<qreal>(0.0, rect().height() - THUMB_RADIUS * 2.0);
  return QRectF((rect().width() - TRACK_THICKNESS) * 0.5, top, TRACK_THICKNESS, height);
}

qreal SeekBar::thumbCenterForValue(int value) const {
  // Map the logical range into track coordinates without involving style metrics.
  const QRectF track = trackRect();
  if (m_maximum <= m_minimum) {
    return m_orientation == Qt::Horizontal ? track.left() : track.bottom();
  }

  const qreal ratio = static_cast<qreal>(value - m_minimum) /
                      static_cast<qreal>(m_maximum - m_minimum);
  if (m_orientation == Qt::Horizontal) {
    return track.left() + ratio * track.width();
  }
  return track.bottom() - ratio * track.height();
}

int SeekBar::displayedThumbPixel(int value) const {
  // Compare in device pixels so HiDPI displays still repaint at visibly smooth steps.
  return static_cast<int>(std::lround(thumbCenterForValue(value) * devicePixelRatioF()));
}

int SeekBar::valueForPosition(const QPointF& pos) const {
  // Convert pointer position back into the logical seek range for click and drag.
  const QRectF track = trackRect();
  if (m_maximum <= m_minimum) {
    return m_minimum;
  }

  qreal ratio = 0.0;
  if (m_orientation == Qt::Horizontal) {
    const qreal x = std::clamp(pos.x(), track.left(), track.right());
    ratio = track.width() > 0.0 ? (x - track.left()) / track.width() : 0.0;
  } else {
    const qreal y = std::clamp(pos.y(), track.top(), track.bottom());
    ratio = track.height() > 0.0 ? (track.bottom() - y) / track.height() : 0.0;
  }

  return m_minimum +
         static_cast<int>(std::lround(ratio * static_cast<qreal>(m_maximum - m_minimum)));
}

QRect SeekBar::dirtyRectForValues(int oldValue, int newValue) const {
  // Limit repaints to the strip touched by the old and new thumb positions.
  const qreal oldCenter = thumbCenterForValue(oldValue);
  const qreal newCenter = thumbCenterForValue(newValue);
  if (m_orientation == Qt::Horizontal) {
    const qreal left = std::min(oldCenter, newCenter) - THUMB_RADIUS - DIRTY_PADDING;
    const qreal right = std::max(oldCenter, newCenter) + THUMB_RADIUS + DIRTY_PADDING;
    return QRectF(left, 0.0, right - left, height()).toAlignedRect().intersected(rect());
  }

  const qreal top = std::min(oldCenter, newCenter) - THUMB_RADIUS - DIRTY_PADDING;
  const qreal bottom = std::max(oldCenter, newCenter) + THUMB_RADIUS + DIRTY_PADDING;
  return QRectF(0.0, top, width(), bottom - top).toAlignedRect().intersected(rect());
}

void SeekBar::updateValueFromPointer(const QPointF& pos) {
  // Dragging is interactive, so emit immediately when the pointer changes the value.
  const int nextValue = valueForPosition(pos);
  if (nextValue == m_value) {
    return;
  }

  const int oldValue = m_value;
  m_value = nextValue;
  update(dirtyRectForValues(oldValue, m_value));
  emit sliderMoved(m_value);
}
