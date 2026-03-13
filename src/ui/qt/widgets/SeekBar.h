/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QColor>
#include <QPen>
#include <QWidget>

class SeekBar : public QWidget {
  Q_OBJECT
public:
  explicit SeekBar(QWidget* parent = nullptr);

  void setRange(int minimum, int maximum);
  void setValue(int value);

  [[nodiscard]] int value() const { return m_value; }
  [[nodiscard]] int minimum() const { return m_minimum; }
  [[nodiscard]] int maximum() const { return m_maximum; }
  [[nodiscard]] bool isSliderDown() const { return m_sliderDown; }

  QSize sizeHint() const override;

signals:
  void sliderMoved(int value);
  void sliderReleased();

protected:
  void changeEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

private:
  [[nodiscard]] QRectF trackRect() const;
  [[nodiscard]] qreal thumbCenterForValue(int value) const;
  [[nodiscard]] int displayedThumbPixel(int value) const;
  [[nodiscard]] int valueForPosition(const QPointF& pos) const;
  [[nodiscard]] QRect dirtyRectForValues(int oldValue, int newValue) const;
  void refreshCachedColors();
  void updateValueFromPointer(const QPointF& pos);

  int m_minimum = 0;
  int m_maximum = 1;
  int m_value = 0;
  bool m_sliderDown = false;
  QColor m_trackColor;
  QColor m_fillColor;
  QColor m_thumbColor;
  QPen m_thumbPen;
};
