/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>

class QMouseEvent;

class PianoRollZoomButtons final : public QWidget {
  Q_OBJECT

public:
  explicit PianoRollZoomButtons(Qt::Orientation orientation, QWidget* parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

signals:
  void zoomInRequested();
  void zoomOutRequested();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class Button {
    None,
    ZoomOut,
    ZoomIn,
  };

  static constexpr int kButtonExtent = 16;

  [[nodiscard]] QRect zoomOutRect() const;
  [[nodiscard]] QRect zoomInRect() const;
  [[nodiscard]] Button buttonAt(const QPoint& pos) const;

  Qt::Orientation m_orientation = Qt::Horizontal;
  Button m_hovered = Button::None;
  Button m_pressed = Button::None;
};
