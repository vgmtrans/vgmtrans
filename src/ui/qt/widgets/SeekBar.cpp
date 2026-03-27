/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SeekBar.h"
#include "UIHelpers.h"

#include <cmath>

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QShowEvent>

#include <algorithm>

namespace {
constexpr qreal TRACK_THICKNESS = 4.0;
constexpr qreal THUMB_RADIUS = 10.0;
constexpr qreal TRACK_RADIUS = TRACK_THICKNESS * 0.5;
constexpr qreal HORIZONTAL_THUMB_Y_OFFSET = 1.0;
constexpr qreal THUMB_SOURCE_PADDING = 1.0;
constexpr qreal THUMB_SHADOW_BLUR_RADIUS = 5.0;
constexpr qreal THUMB_SHADOW_X_OFFSET = 0.0;
constexpr qreal THUMB_SHADOW_Y_OFFSET = 1.0;
constexpr qreal THUMB_SHADOW_EXTENT = 8.0;
constexpr qreal THUMB_SOURCE_SIZE = THUMB_RADIUS * 2.0 + THUMB_SOURCE_PADDING * 2.0;
constexpr qreal THUMB_PIXMAP_SIZE = THUMB_SOURCE_SIZE + THUMB_SHADOW_EXTENT * 2.0;
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
  return QSize(220, 26);
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

bool SeekBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == window() &&
      (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate)) {
    refreshCachedColors();
    repaint();
  }
  return QWidget::eventFilter(watched, event);
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
  ensurePixmaps();

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF dirtyRect = event->rect();
  painter.setClipRect(dirtyRect);

  const qreal centerX = displayedThumbCenterForValue(m_value);
  const QRectF track = trackRect();
  painter.drawPixmap(track.topLeft(), m_trackPixmap);

  QRectF playedRect = track;
  playedRect.setRight(std::max(track.left(), centerX));
  if (!playedRect.isEmpty()) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_fillColor);
    painter.drawRoundedRect(playedRect, TRACK_RADIUS, TRACK_RADIUS);
  }

  const qreal thumbTop = rect().center().y() + HORIZONTAL_THUMB_Y_OFFSET - THUMB_PIXMAP_SIZE * 0.5;
  painter.drawPixmap(QPointF(centerX - THUMB_PIXMAP_SIZE * 0.5, thumbTop), m_thumbPixmap);
}

void SeekBar::showEvent(QShowEvent* event) {
  if (QWidget* topLevel = window(); topLevel && topLevel != this) {
    topLevel->installEventFilter(this);
  }
  QWidget::showEvent(event);
}

void SeekBar::invalidatePixmaps() {
  m_trackPixmapDirty = true;
  m_thumbPixmapDirty = true;
}

void SeekBar::ensurePixmaps() {
  const qreal dpr = devicePixelRatioF();
  const QRectF track = trackRect();
  const QSize logicalTrackSize = track.size().toSize();
  const QSize trackPixelSize = QSizeF(track.width() * dpr, track.height() * dpr).toSize();

  // Redraw the track pixmap if necessary
  if (m_trackPixmapDirty || m_cachedTrackSize != logicalTrackSize || !qFuzzyCompare(m_cachedTrackDpr, dpr)) {
    m_cachedTrackSize = logicalTrackSize;
    m_cachedTrackDpr = dpr;
    m_trackPixmapDirty = false;

    if (trackPixelSize.isEmpty()) {
      m_trackPixmap = QPixmap();
    } else {
      m_trackPixmap = QPixmap(trackPixelSize);
      m_trackPixmap.setDevicePixelRatio(dpr);
      m_trackPixmap.fill(Qt::transparent);

      QPainter trackPainter(&m_trackPixmap);
      trackPainter.setRenderHint(QPainter::Antialiasing, true);
      trackPainter.setPen(Qt::NoPen);
      trackPainter.setBrush(m_trackColor);
      trackPainter.drawRoundedRect(QRectF(0.0, 0.0, track.width(), track.height()),
                                   TRACK_RADIUS,
                                   TRACK_RADIUS);
    }
  }

  // Redraw the thumb pixmap if necessary, otherwise return
  if (!m_thumbPixmapDirty && qFuzzyCompare(m_cachedThumbDpr, dpr)) {
    return;
  }

  m_cachedThumbDpr = dpr;
  m_thumbPixmapDirty = false;

  const QSize thumbPixelSize = QSizeF(THUMB_PIXMAP_SIZE * dpr, THUMB_PIXMAP_SIZE * dpr).toSize();
  if (thumbPixelSize.isEmpty()) {
    m_thumbPixmap = QPixmap();
    return;
  }

  m_thumbPixmap = QPixmap(thumbPixelSize);
  m_thumbPixmap.fill(Qt::transparent);

  const QSize thumbSourcePixelSize = QSizeF(THUMB_SOURCE_SIZE * dpr, THUMB_SOURCE_SIZE * dpr).toSize();
  QPixmap thumbSourcePixmap(thumbSourcePixelSize);
  thumbSourcePixmap.fill(Qt::transparent);

  {
    QPainter thumbPainter(&thumbSourcePixmap);
    thumbPainter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scaledRadius = THUMB_RADIUS * dpr;
    const qreal scaledPadding = THUMB_SOURCE_PADDING * dpr;
    const QPointF center(scaledPadding + scaledRadius, scaledPadding + scaledRadius);
    thumbPainter.setBrush(m_thumbColor);
    thumbPainter.setPen(m_thumbPen);
    thumbPainter.drawEllipse(center, scaledRadius, scaledRadius);
  }

  auto* shadowEffect = new QGraphicsDropShadowEffect;
  shadowEffect->setBlurRadius(THUMB_SHADOW_BLUR_RADIUS * dpr);
  shadowEffect->setOffset(THUMB_SHADOW_X_OFFSET * dpr, THUMB_SHADOW_Y_OFFSET * dpr);
  shadowEffect->setColor(m_thumbShadowColor);
  applyEffectToPixmap(thumbSourcePixmap,
                      m_thumbPixmap,
                      shadowEffect,
                      static_cast<int>(std::ceil(THUMB_SHADOW_EXTENT * dpr)));

  m_thumbPixmap.setDevicePixelRatio(dpr);
}

void SeekBar::refreshCachedColors() {
  // Cache palette-derived tones so playback repaint work stays down to geometry only.
  const QPalette palette = this->palette();
  const bool enabled = isEnabled();
  const bool windowActive = !window() || window()->isActiveWindow();
  const QColor window = palette.color(QPalette::Window);
  const bool darkPalette = window.lightnessF() < 0.5;

  if (!windowActive) {
    m_trackColor = darkPalette ? window.lighter(132) : window.darker(102);
    m_fillColor = darkPalette ? window.lighter(145) : window.darker(120);
    m_thumbColor = darkPalette ? window.lighter(235) : window.lighter(105);
    m_thumbShadowColor = QColor(0, 0, 0, darkPalette ? 36 : 28);
    m_thumbPen = QPen(QColor(0, 0, 0, darkPalette ? 30 : 8));
  } else {
    m_trackColor = darkPalette ? window.lighter(enabled ? 150 : 138)
                               : window.darker(enabled ? 125 : 120);
    m_fillColor = darkPalette ? window.lighter(enabled ? 200 : 150)
                              : window.darker(enabled ? 145 : 132);
    m_thumbColor = darkPalette ? window.lighter(enabled ? 310 : 250)
                               : window.lighter(enabled ? 150 : 102);
    m_thumbShadowColor = QColor(0, 0, 0, darkPalette ? (enabled ? 52 : 36) : (enabled ? 42 : 28));
    m_thumbPen = QPen(QColor(0, 0, 0, darkPalette ? (enabled ? 44 : 30) : (enabled ? 32 : 24)));
  }
  m_thumbPen.setCosmetic(true);
  invalidatePixmaps();
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
  return static_cast<int>(std::lround(thumbCenterForValue(value) * devicePixelRatioF()));
}

qreal SeekBar::displayedThumbCenterForValue(int value) const {
  return static_cast<qreal>(displayedThumbStep(value)) /
         devicePixelRatioF();
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
