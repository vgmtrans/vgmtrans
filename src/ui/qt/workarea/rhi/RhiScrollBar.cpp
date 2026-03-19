/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RhiScrollBar.h"

#include <QPalette>
#include <QTimerEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kScrollbarThickness = 15;
constexpr int kArrowExtent = 15;
constexpr int kMinThumbLength = 24;
constexpr int kThumbInset = 2;
constexpr int kInitialRepeatDelayMs = 280;
constexpr int kRepeatDelayMs = 50;

bool isLightPalette(const QPalette& palette) {
  return qGray(palette.color(QPalette::Window).rgb()) >= 128;
}

QColor laneBorderColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(201, 201, 201) : QColor(82, 82, 82);
}

QColor thumbColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(145, 145, 145) : QColor(170, 170, 170);
}

QColor thumbHoverColor(const QPalette& palette) {
  return thumbColor(palette).lighter(110);
}

QColor thumbPressedColor(const QPalette& palette) {
  return thumbColor(palette).darker(118);
}

QColor buttonHoverColor(const QPalette& palette) {
  const QColor lane = RhiScrollBar::laneColorForPalette(palette);
  return isLightPalette(palette) ? lane.lighter(101) : lane.lighter(108);
}

QColor buttonPressedColor(const QPalette& palette) {
  const QColor lane = RhiScrollBar::laneColorForPalette(palette);
  return isLightPalette(palette) ? lane.darker(104) : lane.lighter(110);
}

QColor glyphColor(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(140, 140, 140) : QColor(196, 196, 196);
}

int axisValue(Qt::Orientation orientation, const QPoint& pos) {
  return (orientation == Qt::Horizontal) ? pos.x() : pos.y();
}
}  // namespace

RhiScrollBar::RhiScrollBar(Qt::Orientation orientation,
                           RedrawCallback requestRedraw,
                           QObject* parent)
    : QObject(parent),
      m_orientation(orientation),
      m_requestRedraw(std::move(requestRedraw)) {
#if defined(Q_OS_WIN)
  m_arrowButtonsVisible = true;
#endif
  updateSnapshot();
}

QColor RhiScrollBar::laneColorForPalette(const QPalette& palette) {
  return isLightPalette(palette) ? QColor(236, 236, 236) : QColor(58, 58, 58);
}

QColor RhiScrollBar::borderColorForPalette(const QPalette& palette) {
  return laneBorderColor(palette);
}

QColor RhiScrollBar::thumbColorForPalette(const QPalette& palette) {
  return thumbColor(palette);
}

QColor RhiScrollBar::thumbHoverColorForPalette(const QPalette& palette) {
  return thumbHoverColor(palette);
}

QColor RhiScrollBar::thumbPressedColorForPalette(const QPalette& palette) {
  return thumbPressedColor(palette);
}

QColor RhiScrollBar::buttonHoverColorForPalette(const QPalette& palette) {
  return buttonHoverColor(palette);
}

QColor RhiScrollBar::buttonPressedColorForPalette(const QPalette& palette) {
  return buttonPressedColor(palette);
}

QColor RhiScrollBar::glyphColorForPalette(const QPalette& palette) {
  return glyphColor(palette);
}

void RhiScrollBar::bindTo(QScrollBar* model) {
  if (m_model == model) {
    return;
  }

  // The chrome can be rebound as views swap models; drop any old model wiring
  // before attaching to the next hidden QScrollBar.
  if (m_model) {
    disconnect(m_model, nullptr, this, nullptr);
  }
  clearDragState();
  stopRepeat();

  m_model = model;
  if (!m_model) {
    setVisible(false);
    return;
  }

  auto markDirty = [this]() {
    refresh();
  };

  connect(m_model, &QScrollBar::valueChanged, this, markDirty);
  connect(m_model, &QScrollBar::rangeChanged, this, markDirty);
  connect(m_model, &QScrollBar::sliderMoved, this, markDirty);
  connect(m_model, &QScrollBar::actionTriggered, this, markDirty);
  connect(m_model, &QObject::destroyed, this, [this]() {
    m_model = nullptr;
    clearDragState();
    stopRepeat();
    setVisible(false);
  });

  syncFromModel();
}

void RhiScrollBar::syncFromModel() {
  refresh();
}

void RhiScrollBar::setArrowButtonsVisible(bool visible) {
  if (m_arrowButtonsVisible == visible) {
    return;
  }
  m_arrowButtonsVisible = visible;
  refresh();
}

void RhiScrollBar::setGeometry(const QRect& rect) {
  if (m_rect == rect) {
    return;
  }
  m_rect = rect;
  refresh();
}

void RhiScrollBar::setVisible(bool visible) {
  if (m_visible == visible) {
    return;
  }
  m_visible = visible;
  if (!m_visible) {
    clearDragState();
    stopRepeat();
    m_pressedPart = Part::None;
    m_hoveredPart = Part::None;
  }
  refresh();
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
  if (!m_arrowButtonsVisible || !m_visible) {
    return {};
  }

  if (m_orientation == Qt::Horizontal) {
    return QRect(m_rect.left(), m_rect.top(), std::min(kArrowExtent, m_rect.width()), m_rect.height());
  }
  return QRect(m_rect.left(), m_rect.top(), m_rect.width(), std::min(kArrowExtent, m_rect.height()));
}

QRect RhiScrollBar::trailingArrowRect() const {
  if (!m_arrowButtonsVisible || !m_visible) {
    return {};
  }

  if (m_orientation == Qt::Horizontal) {
    const int extent = std::min(kArrowExtent, m_rect.width());
    return QRect(std::max(m_rect.left(), m_rect.right() - extent + 1), m_rect.top(), extent, m_rect.height());
  }
  const int extent = std::min(kArrowExtent, m_rect.height());
  return QRect(m_rect.left(), std::max(m_rect.top(), m_rect.bottom() - extent + 1), m_rect.width(), extent);
}

QRect RhiScrollBar::trackRect() const {
  QRect rect = m_rect.adjusted(0, 0, -1, -1);
  if (m_arrowButtonsVisible) {
    if (m_orientation == Qt::Horizontal) {
      rect.adjust(leadingArrowRect().width(), 0, -trailingArrowRect().width(), 0);
    } else {
      rect.adjust(0, leadingArrowRect().height(), 0, -trailingArrowRect().height());
    }
  }
  return rect.adjusted(1, 1, -1, -1);
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
  const int minThumbLength = std::min(kMinThumbLength, trackLength);
  // Match standard scrollbar behavior: thumb size reflects how much content is
  // visible, while thumb travel reflects the remaining scrollable range.
  const int thumbLength = std::clamp(
      static_cast<int>(std::lround((static_cast<double>(pageStep) / static_cast<double>(fullRange)) * trackLength)),
      minThumbLength,
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
    const int thumbHeight = std::max(8, track.height() - (2 * kThumbInset));
    const int thumbTop = track.top() + ((track.height() - thumbHeight) / 2);
    metrics.rect = QRect(thumbStart, thumbTop, thumbLength, thumbHeight);
  } else {
    const int thumbWidth = std::max(8, track.width() - (2 * kThumbInset));
    const int thumbLeft = track.left() + ((track.width() - thumbWidth) / 2);
    metrics.rect = QRect(thumbLeft, thumbStart, thumbWidth, thumbLength);
  }

  return metrics;
}

RhiScrollBar::Part RhiScrollBar::partAt(const QPoint& pos) const {
  if (!m_visible || !m_rect.contains(pos)) {
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

void RhiScrollBar::refresh() {
  updateSnapshot();
  requestRedraw();
}

void RhiScrollBar::updateSnapshot() {
  m_snapshot = {};
  m_snapshot.visible = m_visible;
  m_snapshot.orientation = m_orientation;
  m_snapshot.laneRect = QRectF(m_rect);
  if (!m_visible || m_rect.isEmpty()) {
    return;
  }

  // Snapshot data is the only thing the renderer sees, so fold all current
  // hover/press/geometry state into it here.
  const ThumbMetrics metrics = thumbMetrics();
  m_snapshot.thumbRect = QRectF(metrics.rect);
  m_snapshot.thumbHovered = (m_hoveredPart == Part::Thumb) || m_draggingThumb;
  m_snapshot.thumbPressed = (m_pressedPart == Part::Thumb) || m_draggingThumb;

  if (m_arrowButtonsVisible) {
    const QRect leadingRect = leadingArrowRect();
    if (!leadingRect.isEmpty()) {
      m_snapshot.arrowButtons.push_back(RhiScrollButtonSnapshot{
          QRectF(leadingRect),
          RhiScrollButtonGlyph::Backward,
          m_hoveredPart == Part::LeadingArrow,
          m_pressedPart == Part::LeadingArrow,
          true,
      });
    }
    const QRect trailingRect = trailingArrowRect();
    if (!trailingRect.isEmpty()) {
      m_snapshot.arrowButtons.push_back(RhiScrollButtonSnapshot{
          QRectF(trailingRect),
          RhiScrollButtonGlyph::Forward,
          m_hoveredPart == Part::TrailingArrow,
          m_pressedPart == Part::TrailingArrow,
          true,
      });
    }
  }
}

void RhiScrollBar::requestRedraw() const {
  if (m_requestRedraw) {
    m_requestRedraw();
  }
}

bool RhiScrollBar::handleMousePress(const QPoint& pos) {
  if (!m_visible || !m_model) {
    return false;
  }

  const Part part = partAt(pos);
  m_pressedPart = part;
  m_hoveredPart = part;
  if (part == Part::Thumb) {
    m_draggingThumb = true;
    m_dragThumbOffset = axisValue(m_orientation, pos) -
                        axisValue(m_orientation, thumbMetrics().rect.topLeft());
    m_model->setSliderDown(true);
  } else if (part != Part::None) {
    triggerAction(part);
    startRepeat(part);
  }

  if (part != Part::None) {
    refresh();
    return true;
  }
  return false;
}

bool RhiScrollBar::handleMouseMove(const QPoint& pos, Qt::MouseButtons buttons) {
  if (!m_visible || !m_model) {
    return false;
  }

  if (m_draggingThumb) {
    if (!(buttons & Qt::LeftButton)) {
      clearDragState();
      m_pressedPart = Part::None;
      m_hoveredPart = partAt(pos);
      refresh();
      return false;
    }
    // While dragging we forward thumb motion straight into the hidden model.
    m_model->setSliderPosition(valueForPointer(pos));
    return true;
  }

  const Part hovered = partAt(pos);
  if (hovered != m_hoveredPart) {
    m_hoveredPart = hovered;
    refresh();
  }
  return hovered != Part::None;
}

bool RhiScrollBar::handleMouseRelease(const QPoint& pos) {
  if (!m_visible || !m_model) {
    return false;
  }

  const bool wasActive = m_draggingThumb || (m_pressedPart != Part::None);
  clearDragState();
  stopRepeat();
  m_pressedPart = Part::None;
  m_hoveredPart = partAt(pos);
  updateSnapshot();
  if (wasActive) {
    requestRedraw();
  }
  return wasActive;
}

void RhiScrollBar::handleLeave() {
  if (!m_draggingThumb && m_hoveredPart != Part::None) {
    m_hoveredPart = Part::None;
    refresh();
  }
}

void RhiScrollBar::timerEvent(QTimerEvent* event) {
  if (!event || event->timerId() != m_repeatTimer.timerId()) {
    QObject::timerEvent(event);
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
