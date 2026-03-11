/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QMargins>
#include <QMetaObject>
#include <QPoint>
#include <Qt>

#include <functional>
#include <memory>
#include <vector>

#include "RhiScrollChromeData.h"

class QAbstractScrollArea;
class QScrollBar;
class RhiScrollBar;

class RhiScrollAreaChrome final {
public:
  using ViewportMarginsApplier = std::function<void(const QMargins& margins)>;
  using RedrawCallback = std::function<void()>;

  struct ButtonSpec {
    RhiScrollButtonGlyph glyph = RhiScrollButtonGlyph::Minus;
    std::function<void()> onPressed;
  };

  explicit RhiScrollAreaChrome(QAbstractScrollArea* area,
                               ViewportMarginsApplier applyViewportMargins,
                               RedrawCallback requestRedraw);
  ~RhiScrollAreaChrome();

  void setHorizontalPolicy(Qt::ScrollBarPolicy policy);
  void setVerticalPolicy(Qt::ScrollBarPolicy policy);
  [[nodiscard]] Qt::ScrollBarPolicy horizontalPolicy() const { return m_horizontalPolicy; }
  [[nodiscard]] Qt::ScrollBarPolicy verticalPolicy() const { return m_verticalPolicy; }
  void setHorizontalArrowButtonsVisible(bool visible);
  void setVerticalArrowButtonsVisible(bool visible);
  void setHorizontalButtons(std::vector<ButtonSpec> buttons);
  void setVerticalButtons(std::vector<ButtonSpec> buttons);

  void syncLayout();
  [[nodiscard]] const RhiScrollAreaChromeSnapshot& snapshot() const { return m_snapshot; }

  [[nodiscard]] bool handleMousePress(const QPoint& pos);
  [[nodiscard]] bool handleMouseMove(const QPoint& pos, Qt::MouseButtons buttons);
  [[nodiscard]] bool handleMouseRelease(const QPoint& pos);
  void handleLeave();

private:
  // Hidden QScrollBar models still own range/value/page-step; this class only
  // lays out and hit-tests the chrome that gets drawn inside the RHI surface.
  [[nodiscard]] bool shouldShowHorizontalBar() const;
  [[nodiscard]] bool shouldShowVerticalBar() const;
  [[nodiscard]] QRect horizontalButtonsRect() const;
  [[nodiscard]] QRect verticalButtonsRect() const;
  [[nodiscard]] QRect buttonRect(Qt::Orientation orientation, int index) const;
  [[nodiscard]] int buttonIndexAt(Qt::Orientation orientation, const QPoint& pos) const;
  void updateSnapshot();
  void requestRedraw() const;

  QAbstractScrollArea* m_area = nullptr;
  ViewportMarginsApplier m_applyViewportMargins;
  RedrawCallback m_requestRedraw;
  std::unique_ptr<RhiScrollBar> m_horizontalBar;
  std::unique_ptr<RhiScrollBar> m_verticalBar;
  std::vector<ButtonSpec> m_horizontalButtons;
  std::vector<ButtonSpec> m_verticalButtons;
  int m_hoveredHorizontalButton = -1;
  int m_pressedHorizontalButton = -1;
  int m_hoveredVerticalButton = -1;
  int m_pressedVerticalButton = -1;
  QMetaObject::Connection m_horizontalRangeConnection;
  QMetaObject::Connection m_verticalRangeConnection;
  Qt::ScrollBarPolicy m_horizontalPolicy = Qt::ScrollBarAlwaysOff;
  Qt::ScrollBarPolicy m_verticalPolicy = Qt::ScrollBarAlwaysOff;
  RhiScrollAreaChromeSnapshot m_snapshot;
};
