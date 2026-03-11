/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RhiScrollBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QStyle>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kScrollbarThickness = 16;
constexpr int kArrowExtent = 16;
constexpr int kMinThumbLength = 28;
constexpr int kThumbInset = 4;
constexpr int kInitialRepeatDelayMs = 280;
constexpr int kRepeatDelayMs = 50;

bool isLightPalette(const QPalette& palette) {
  return qGray(palette.color(QPalette::Window).rgb()) >= 128;
}

QColor laneBorderColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(201, 201, 201) : QColor(82, 82, 82);
}

QColor trackColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(198, 198, 198) : QColor(116, 116, 116);
}

QColor thumbColor(const QPalette& palette, bool hovered, bool pressed) {
  QColor color = isLightPalette(palette) ? QColor(145, 145, 145) : QColor(170, 170, 170);
  if (pressed) {
    return color.darker(118);
  }
  if (hovered) {
    return color.lighter(110);
  }
  return color;
}

QColor buttonFillColor(const QPalette& palette, bool hovered, bool pressed) {
  QColor color = RhiScrollBar::laneColorForPalette(palette);
  if (pressed) {
    return isLightPalette(palette) ? color.darker(104) : color.lighter(110);
  }
  if (hovered) {
    return isLightPalette(palette) ? color.lighter(101) : color.lighter(108);
  }
  return color;
}

QColor glyphColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(140, 140, 140) : QColor(196, 196, 196);
}

int axisValue(Qt::Orientation orientation, const QPoint& pos) {
  return (orientation == Qt::Horizontal) ? pos.x() : pos.y();
}
}  // namespace

RhiScrollBar::RhiScrollBar(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent),
      m_orientation(orientation) {
#if defined(Q_OS_WIN)
  m_arrowButtonsVisible = true;
#endif
  setMouseTracking(true);
}

QColor RhiScrollBar::laneColorForPalette(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(236, 236, 236) : QColor(58, 58, 58);
}

QColor RhiScrollBar::borderColorForPalette(const QPalette& palette) {
  return laneBorderColor(palette);
}

QColor RhiScrollBar::glyphColorForPalette(const QPalette& palette) {
  return glyphColor(palette);
}

void RhiScrollBar::bindTo(QScrollBar* model) {
  if (m_model == model) {
    return;
  }

  for (const QMetaObject::Connection& connection : m_modelConnections) {
    disconnect(connection);
  }
  m_modelConnections.clear();
  clearDragState();
  stopRepeat();

  m_model = model;
  if (!m_model) {
    hide();
    return;
  }

  m_modelConnections.push_back(connect(m_model, &QScrollBar::valueChanged, this, [this]() {
    update();
  }));
  m_modelConnections.push_back(connect(m_model, &QScrollBar::rangeChanged, this, [this]() {
    update();
  }));
  m_modelConnections.push_back(connect(m_model, &QScrollBar::sliderMoved, this, [this]() {
    update();
  }));
  m_modelConnections.push_back(connect(m_model, &QScrollBar::actionTriggered, this, [this]() {
    update();
  }));
  m_modelConnections.push_back(connect(m_model, &QObject::destroyed, this, [this]() {
    m_model = nullptr;
    clearDragState();
    stopRepeat();
    hide();
  }));

  syncFromModel();
}

void RhiScrollBar::syncFromModel() {
  updateGeometry();
  update();
}

void RhiScrollBar::setArrowButtonsVisible(bool visible) {
  if (m_arrowButtonsVisible == visible) {
    return;
  }
  m_arrowButtonsVisible = visible;
  updateGeometry();
  update();
}

QSize RhiScrollBar::sizeHint() const {
  return (m_orientation == Qt::Horizontal)
      ? QSize(120, kScrollbarThickness)
      : QSize(kScrollbarThickness, 120);
}

QSize RhiScrollBar::minimumSizeHint() const {
  const int minAxis = m_arrowButtonsVisible ? ((2 * kArrowExtent) + 24) : 32;
  return (m_orientation == Qt::Horizontal)
      ? QSize(minAxis, kScrollbarThickness)
      : QSize(kScrollbarThickness, minAxis);
}

QRect RhiScrollBar::leadingArrowRect() const {
  if (!m_arrowButtonsVisible) {
    return {};
  }

  if (m_orientation == Qt::Horizontal) {
    return QRect(0, 0, std::min(kArrowExtent, width()), height());
  }
  return QRect(0, 0, width(), std::min(kArrowExtent, height()));
}

QRect RhiScrollBar::trailingArrowRect() const {
  if (!m_arrowButtonsVisible) {
    return {};
  }

  if (m_orientation == Qt::Horizontal) {
    const int extent = std::min(kArrowExtent, width());
    return QRect(std::max(0, width() - extent), 0, extent, height());
  }
  const int extent = std::min(kArrowExtent, height());
  return QRect(0, std::max(0, height() - extent), width(), extent);
}

QRect RhiScrollBar::trackRect() const {
  QRect rect = this->rect().adjusted(0, 0, -1, -1);
  if (m_arrowButtonsVisible) {
    if (m_orientation == Qt::Horizontal) {
      rect.adjust(leadingArrowRect().width(), 0, -trailingArrowRect().width(), 0);
    } else {
      rect.adjust(0, leadingArrowRect().height(), 0, -trailingArrowRect().height());
    }
  }
  return rect.adjusted(2, 2, -2, -2);
}

RhiScrollBar::ThumbMetrics RhiScrollBar::thumbMetrics() const {
  ThumbMetrics metrics;
  const QRect track = trackRect();
  if (!m_model || track.width() <= 0 || track.height() <= 0) {
    return metrics;
  }

  const int minValue = m_model->minimum();
  const int maxValue = m_model->maximum();
  const int pageStep = std::max(1, m_model->pageStep());
  const int range = std::max(0, maxValue - minValue);
  const int trackLength = (m_orientation == Qt::Horizontal) ? track.width() : track.height();
  const int fullRange = std::max(1, range + pageStep);
  const int thumbLength = std::clamp(
      static_cast<int>(std::lround((static_cast<double>(pageStep) / static_cast<double>(fullRange)) * trackLength)),
      kMinThumbLength,
      trackLength);
  const int travel = std::max(0, trackLength - thumbLength);
  const double ratio = (range <= 0)
      ? 0.0
      : static_cast<double>(m_model->value() - minValue) / static_cast<double>(range);
  const int thumbStart = ((m_orientation == Qt::Horizontal) ? track.left() : track.top()) +
                         static_cast<int>(std::lround(ratio * travel));

  metrics.trackStart = (m_orientation == Qt::Horizontal) ? track.left() : track.top();
  metrics.trackLength = trackLength;
  metrics.thumbLength = thumbLength;

  if (m_orientation == Qt::Horizontal) {
    const int thumbHeight = std::max(6, track.height() - (2 * kThumbInset));
    const int thumbTop = track.top() + ((track.height() - thumbHeight) / 2);
    metrics.rect = QRect(thumbStart, thumbTop, thumbLength, thumbHeight);
  } else {
    const int thumbWidth = std::max(6, track.width() - (2 * kThumbInset));
    const int thumbLeft = track.left() + ((track.width() - thumbWidth) / 2);
    metrics.rect = QRect(thumbLeft, thumbStart, thumbWidth, thumbLength);
  }

  return metrics;
}

RhiScrollBar::Part RhiScrollBar::partAt(const QPoint& pos) const {
  if (!rect().contains(pos)) {
    return Part::None;
  }
  if (leadingArrowRect().contains(pos)) {
    return Part::LeadingArrow;
  }
  if (trailingArrowRect().contains(pos)) {
    return Part::TrailingArrow;
  }

  const ThumbMetrics metrics = thumbMetrics();
  if (metrics.rect.contains(pos)) {
    return Part::Thumb;
  }

  const QRect track = trackRect();
  if (!track.contains(pos)) {
    return Part::None;
  }

  if (m_orientation == Qt::Horizontal) {
    return (pos.x() < metrics.rect.left()) ? Part::TrackBackward : Part::TrackForward;
  }
  return (pos.y() < metrics.rect.top()) ? Part::TrackBackward : Part::TrackForward;
}

int RhiScrollBar::valueForPointer(const QPoint& pos) const {
  if (!m_model) {
    return 0;
  }

  const ThumbMetrics metrics = thumbMetrics();
  const int minValue = m_model->minimum();
  const int maxValue = m_model->maximum();
  const int range = std::max(0, maxValue - minValue);
  if (range <= 0) {
    return minValue;
  }

  const int travel = std::max(1, metrics.trackLength - metrics.thumbLength);
  const int pointerPos = axisValue(m_orientation, pos) - m_dragThumbOffset;
  const int pointerDelta = std::clamp(pointerPos - metrics.trackStart, 0, travel);
  const double ratio = static_cast<double>(pointerDelta) / static_cast<double>(travel);
  return minValue + static_cast<int>(std::lround(ratio * range));
}

void RhiScrollBar::triggerAction(Part part) {
  if (!m_model) {
    return;
  }

  switch (part) {
    case Part::LeadingArrow:
      m_model->triggerAction(QAbstractSlider::SliderSingleStepSub);
      break;
    case Part::TrailingArrow:
      m_model->triggerAction(QAbstractSlider::SliderSingleStepAdd);
      break;
    case Part::TrackBackward:
      m_model->triggerAction(QAbstractSlider::SliderPageStepSub);
      break;
    case Part::TrackForward:
      m_model->triggerAction(QAbstractSlider::SliderPageStepAdd);
      break;
    default:
      break;
  }
}

void RhiScrollBar::startRepeat(Part part) {
  if (part == Part::None || part == Part::Thumb) {
    return;
  }

  m_pressedPart = part;
  m_repeatFast = false;
  m_repeatTimer.start(kInitialRepeatDelayMs, this);
}

void RhiScrollBar::stopRepeat() {
  m_repeatFast = false;
  m_repeatTimer.stop();
}

void RhiScrollBar::clearDragState() {
  if (m_model && m_draggingThumb) {
    m_model->setSliderDown(false);
  }
  m_draggingThumb = false;
  m_dragThumbOffset = 0;
}

void RhiScrollBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), laneColorForPalette(palette()));

  const QColor border = borderColorForPalette(palette());
  painter.setPen(border);
  painter.drawRect(rect().adjusted(0, 0, -1, -1));

  if (m_arrowButtonsVisible) {
    const QRect leading = leadingArrowRect().adjusted(0, 0, -1, -1);
    const QRect trailing = trailingArrowRect().adjusted(0, 0, -1, -1);
    painter.fillRect(leading, buttonFillColor(palette(),
                                              m_hoveredPart == Part::LeadingArrow,
                                              m_pressedPart == Part::LeadingArrow));
    painter.fillRect(trailing, buttonFillColor(palette(),
                                               m_hoveredPart == Part::TrailingArrow,
                                               m_pressedPart == Part::TrailingArrow));

    painter.setPen(border);
    if (m_orientation == Qt::Horizontal) {
      painter.drawLine(leading.right(), leading.top(), leading.right(), leading.bottom());
      painter.drawLine(trailing.left(), trailing.top(), trailing.left(), trailing.bottom());
    } else {
      painter.drawLine(leading.left(), leading.bottom(), leading.right(), leading.bottom());
      painter.drawLine(trailing.left(), trailing.top(), trailing.right(), trailing.top());
    }

    painter.setBrush(glyphColorForPalette(palette()));
    painter.setPen(Qt::NoPen);
    auto drawArrow = [&](const QRect& buttonRect, bool leadingArrow) {
      const int half = std::max(3, std::min(buttonRect.width(), buttonRect.height()) / 4);
      const QPoint center = buttonRect.center();
      QPolygon triangle;
      if (m_orientation == Qt::Horizontal) {
        if (leadingArrow) {
          triangle << QPoint(center.x() - half, center.y())
                   << QPoint(center.x() + half, center.y() - half)
                   << QPoint(center.x() + half, center.y() + half);
        } else {
          triangle << QPoint(center.x() + half, center.y())
                   << QPoint(center.x() - half, center.y() - half)
                   << QPoint(center.x() - half, center.y() + half);
        }
      } else if (leadingArrow) {
        triangle << QPoint(center.x(), center.y() - half)
                 << QPoint(center.x() - half, center.y() + half)
                 << QPoint(center.x() + half, center.y() + half);
      } else {
        triangle << QPoint(center.x(), center.y() + half)
                 << QPoint(center.x() - half, center.y() - half)
                 << QPoint(center.x() + half, center.y() - half);
      }
      painter.drawPolygon(triangle);
    };
    drawArrow(leadingArrowRect(), true);
    drawArrow(trailingArrowRect(), false);
  }

  const QRect track = trackRect();
  if (!track.isEmpty()) {
    const QRect centerTrack = (m_orientation == Qt::Horizontal)
        ? QRect(track.left(), track.center().y() - 2, track.width(), 4)
        : QRect(track.center().x() - 2, track.top(), 4, track.height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor(palette()));
    painter.drawRoundedRect(centerTrack, 2.0, 2.0);
  }

  if (!m_model) {
    return;
  }

  const ThumbMetrics metrics = thumbMetrics();
  if (metrics.rect.isEmpty()) {
    return;
  }

  painter.setBrush(thumbColor(palette(),
                              m_hoveredPart == Part::Thumb || m_draggingThumb,
                              m_pressedPart == Part::Thumb || m_draggingThumb));
  painter.setPen(Qt::NoPen);
  const qreal radius = (m_orientation == Qt::Horizontal)
      ? (metrics.rect.height() / 2.0)
      : (metrics.rect.width() / 2.0);
  painter.drawRoundedRect(metrics.rect, radius, radius);
}

void RhiScrollBar::mousePressEvent(QMouseEvent* event) {
  if (!event || event->button() != Qt::LeftButton || !m_model) {
    QWidget::mousePressEvent(event);
    return;
  }

  const Part part = partAt(event->pos());
  m_pressedPart = part;
  m_hoveredPart = part;
  if (part == Part::Thumb) {
    m_draggingThumb = true;
    m_dragThumbOffset = axisValue(m_orientation, event->pos()) -
                        axisValue(m_orientation, thumbMetrics().rect.topLeft());
    m_model->setSliderDown(true);
  } else if (part != Part::None) {
    triggerAction(part);
    startRepeat(part);
  }
  update();
  event->accept();
}

void RhiScrollBar::mouseMoveEvent(QMouseEvent* event) {
  if (!event) {
    QWidget::mouseMoveEvent(event);
    return;
  }

  if (m_draggingThumb && m_model) {
    m_model->setSliderPosition(valueForPointer(event->pos()));
    event->accept();
    return;
  }

  const Part hovered = partAt(event->pos());
  if (hovered != m_hoveredPart) {
    m_hoveredPart = hovered;
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void RhiScrollBar::mouseReleaseEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
    clearDragState();
    stopRepeat();
    m_pressedPart = Part::None;
    m_hoveredPart = event ? partAt(event->pos()) : Part::None;
    update();
    event->accept();
    return;
  }

  QWidget::mouseReleaseEvent(event);
}

void RhiScrollBar::leaveEvent(QEvent* event) {
  if (!m_draggingThumb) {
    m_hoveredPart = Part::None;
    update();
  }
  QWidget::leaveEvent(event);
}

void RhiScrollBar::timerEvent(QTimerEvent* event) {
  if (!event || event->timerId() != m_repeatTimer.timerId()) {
    QWidget::timerEvent(event);
    return;
  }

  if (m_pressedPart == Part::None || m_pressedPart == Part::Thumb) {
    stopRepeat();
    return;
  }

  triggerAction(m_pressedPart);
  if (!m_repeatFast) {
    m_repeatFast = true;
    m_repeatTimer.start(kRepeatDelayMs, this);
  }
}
