/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QBasicTimer>
#include <QColor>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QWidget>

#include <vector>

class QMouseEvent;
class QPaintEvent;

class RhiScrollBar final : public QWidget {
public:
  explicit RhiScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);

  void bindTo(QScrollBar* model);
  [[nodiscard]] QScrollBar* model() const { return m_model; }
  void syncFromModel();

  void setArrowButtonsVisible(bool visible);
  [[nodiscard]] bool arrowButtonsVisible() const { return m_arrowButtonsVisible; }
  [[nodiscard]] Qt::Orientation orientation() const { return m_orientation; }

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  static QColor laneColorForPalette(const QPalette& palette);
  static QColor borderColorForPalette(const QPalette& palette);
  static QColor glyphColorForPalette(const QPalette& palette);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void timerEvent(QTimerEvent* event) override;

private:
  enum class Part {
    None,
    LeadingArrow,
    TrailingArrow,
    TrackBackward,
    TrackForward,
    Thumb,
  };

  struct ThumbMetrics {
    QRect rect;
    int trackStart = 0;
    int trackLength = 0;
    int thumbLength = 0;
  };

  [[nodiscard]] QRect leadingArrowRect() const;
  [[nodiscard]] QRect trailingArrowRect() const;
  [[nodiscard]] QRect trackRect() const;
  [[nodiscard]] ThumbMetrics thumbMetrics() const;
  [[nodiscard]] Part partAt(const QPoint& pos) const;
  [[nodiscard]] int valueForPointer(const QPoint& pos) const;
  void triggerAction(Part part);
  void startRepeat(Part part);
  void stopRepeat();
  void clearDragState();

  Qt::Orientation m_orientation = Qt::Horizontal;
  QPointer<QScrollBar> m_model;
  std::vector<QMetaObject::Connection> m_modelConnections;
  QBasicTimer m_repeatTimer;
  bool m_repeatFast = false;
  bool m_arrowButtonsVisible = false;
  bool m_draggingThumb = false;
  int m_dragThumbOffset = 0;
  Part m_pressedPart = Part::None;
  Part m_hoveredPart = Part::None;
};
