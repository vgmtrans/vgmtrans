/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QBasicTimer>
#include <QObject>
#include <QPointer>
#include <QScrollBar>

#include <functional>

#include "RhiScrollChromeData.h"

class RhiScrollBar final : public QObject {
public:
  using RedrawCallback = std::function<void()>;

  explicit RhiScrollBar(Qt::Orientation orientation,
                        RedrawCallback requestRedraw,
                        QObject* parent = nullptr);

  void bindTo(QScrollBar* model);
  [[nodiscard]] QScrollBar* model() const { return m_model; }
  void syncFromModel();

  void setArrowButtonsVisible(bool visible);
  [[nodiscard]] bool arrowButtonsVisible() const { return m_arrowButtonsVisible; }
  [[nodiscard]] Qt::Orientation orientation() const { return m_orientation; }

  void setGeometry(const QRect& rect);
  void setVisible(bool visible);
  [[nodiscard]] QSize sizeHint() const;
  [[nodiscard]] QSize minimumSizeHint() const;
  [[nodiscard]] const RhiScrollBarSnapshot& snapshot() const { return m_snapshot; }

  [[nodiscard]] bool handleMousePress(const QPoint& pos);
  [[nodiscard]] bool handleMouseMove(const QPoint& pos, Qt::MouseButtons buttons);
  [[nodiscard]] bool handleMouseRelease(const QPoint& pos);
  void handleLeave();

  static QColor laneColorForPalette(const QPalette& palette);
  static QColor borderColorForPalette(const QPalette& palette);
  static QColor thumbColorForPalette(const QPalette& palette);
  static QColor thumbHoverColorForPalette(const QPalette& palette);
  static QColor thumbPressedColorForPalette(const QPalette& palette);
  static QColor buttonHoverColorForPalette(const QPalette& palette);
  static QColor buttonPressedColorForPalette(const QPalette& palette);
  static QColor glyphColorForPalette(const QPalette& palette);

protected:
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
  void refresh();
  void updateSnapshot();
  void requestRedraw() const;

  Qt::Orientation m_orientation = Qt::Horizontal;
  RedrawCallback m_requestRedraw;
  QPointer<QScrollBar> m_model;
  QRect m_rect;
  QBasicTimer m_repeatTimer;
  RhiScrollBarSnapshot m_snapshot;
  bool m_repeatFast = false;
  bool m_arrowButtonsVisible = false;
  bool m_draggingThumb = false;
  bool m_visible = false;
  int m_dragThumbOffset = 0;
  Part m_pressedPart = Part::None;
  Part m_hoveredPart = Part::None;
};
