/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SeekBar.h"
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <algorithm>

namespace {
constexpr qreal TRACK_THICKNESS = 4.0;
constexpr qreal THUMB_RADIUS = 10.0;
constexpr qreal TRACK_RADIUS = TRACK_THICKNESS * 0.5;
constexpr qreal HORIZONTAL_THUMB_Y_OFFSET = 1.0;
constexpr qreal DISPLAY_STEPS_PER_DEVICE_PIXEL = 2.0;
constexpr int DIRTY_PADDING = 2;
}

SeekBar::SeekBar(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_NoSystemBackground);
  refreshCachedColors();
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
  m_maximum = maximum;
  m_value = std::clamp(m_value, m_minimum, m_maximum);
  update();
}

void SeekBar::setValue(int value) {
  // Passive playback only repaints when the quantized displayed thumb position changes.
  const int clamped = std::clamp(value, m_minimum, m_maximum);
  if (m_value == clamped) {
    return;
  }

  const int oldValue = m_value;
  m_value = clamped;
  if (displayedThumbStep(oldValue) == displayedThumbStep(m_value)) {
    return;
  }
  update(dirtyRectForValues(oldValue, m_value));
}

QSize SeekBar::sizeHint() const {
  return QSize(160, 26);
}

void SeekBar::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  switch (event->type()) {
    case QEvent::EnabledChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
      refreshCachedColors();
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

  painter.setPen(Qt::NoPen);
  painter.setBrush(m_trackColor);
  painter.drawRoundedRect(track, TRACK_RADIUS, TRACK_RADIUS);

  if (m_maximum > m_minimum) {
    const qreal thumbCenter = displayedThumbCenterForValue(m_value);
    QRectF played = track;
    played.setRight(std::max(track.left(), thumbCenter));
    if (played.width() > 0.0 && played.height() > 0.0) {
      painter.setBrush(m_fillColor);
      painter.drawRoundedRect(played, TRACK_RADIUS, TRACK_RADIUS);
    }
  }

  painter.setBrush(m_thumbColor);
  painter.setPen(m_thumbPen);
  const qreal centerX = displayedThumbCenterForValue(m_value);
  painter.drawEllipse(QPointF(centerX, rect().center().y() + HORIZONTAL_THUMB_Y_OFFSET),
                      THUMB_RADIUS,
                      THUMB_RADIUS);
}

void SeekBar::refreshCachedColors() {
  // Cache palette-derived tones so playback repaint work stays down to geometry only.
  const QPalette palette = this->palette();
  const QColor window = palette.color(QPalette::Window);
  const bool enabled = isEnabled();
  const bool darkPalette = window.lightnessF() < 0.5;

  m_trackColor = darkPalette ? window.lighter(enabled ? 150 : 125)
                             : window.darker(enabled ? 125 : 120);
  m_fillColor = darkPalette ? window.lighter(enabled ? 225 : 150)
                            : window.darker(enabled ? 145 : 132);
  m_thumbColor = darkPalette ? window.lighter(enabled ? 350 : 250)
                             : window.lighter(enabled ? 150 : 102);

  m_thumbPen = QPen(QColor(0, 0, 0, darkPalette ? 55 : 100));
  m_thumbPen.setWidth(1);
  m_thumbPen.setCosmetic(true);
}

QRectF SeekBar::trackRect() const {
  // Reserve thumb radius at both ends so the handle stays fully inside the widget.
  const qreal left = THUMB_RADIUS;
  const qreal width = std::max<qreal>(0.0, rect().width() - THUMB_RADIUS * 2.0);
  return QRectF(left, (rect().height() - TRACK_THICKNESS) * 0.5, width, TRACK_THICKNESS);
}

qreal SeekBar::thumbCenterForValue(int value) const {
  // Map the logical range into track coordinates without involving style metrics.
  const QRectF track = trackRect();
  if (m_maximum <= m_minimum) {
    return track.left();
  }

  const qreal ratio = static_cast<qreal>(value - m_minimum) /
                      static_cast<qreal>(m_maximum - m_minimum);
  return track.left() + ratio * track.width();
}

int SeekBar::displayedThumbStep(int value) const {
  // Quantize the rendered thumb position to half-device-pixel steps.
  return static_cast<int>(
      std::lround(thumbCenterForValue(value) * devicePixelRatioF() * DISPLAY_STEPS_PER_DEVICE_PIXEL));
}

qreal SeekBar::displayedThumbCenterForValue(int value) const {
  return static_cast<qreal>(displayedThumbStep(value)) /
         (devicePixelRatioF() * DISPLAY_STEPS_PER_DEVICE_PIXEL);
}

int SeekBar::valueForPosition(const QPointF& pos) const {
  // Convert pointer position back into the logical seek range for click and drag.
  const QRectF track = trackRect();
  if (m_maximum <= m_minimum) {
    return m_minimum;
  }

  const qreal x = std::clamp(pos.x(), track.left(), track.right());
  const qreal ratio = track.width() > 0.0 ? (x - track.left()) / track.width() : 0.0;

  return m_minimum + static_cast<int>(std::lround(ratio * static_cast<qreal>(m_maximum - m_minimum)));
}

QRect SeekBar::dirtyRectForValues(int oldValue, int newValue) const {
  // Limit repaints to the strip touched by the old and new thumb positions.
  const qreal oldCenter = displayedThumbCenterForValue(oldValue);
  const qreal newCenter = displayedThumbCenterForValue(newValue);
  const qreal left = std::min(oldCenter, newCenter) - THUMB_RADIUS - DIRTY_PADDING;
  const qreal right = std::max(oldCenter, newCenter) + THUMB_RADIUS + DIRTY_PADDING;
  return QRectF(left, 0.0, right - left, height()).toAlignedRect().intersected(rect());
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
