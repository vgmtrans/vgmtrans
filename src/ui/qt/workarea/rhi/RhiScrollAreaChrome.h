/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QMargins>
#include <Qt>

#include <functional>

class QAbstractScrollArea;
class QWidget;
class RhiScrollBar;

class RhiScrollAreaChrome final {
public:
  using ViewportMarginsApplier = std::function<void(const QMargins& margins)>;

  explicit RhiScrollAreaChrome(QAbstractScrollArea* area, ViewportMarginsApplier applyViewportMargins);
  ~RhiScrollAreaChrome();

  void setHorizontalPolicy(Qt::ScrollBarPolicy policy);
  void setVerticalPolicy(Qt::ScrollBarPolicy policy);
  [[nodiscard]] Qt::ScrollBarPolicy horizontalPolicy() const { return m_horizontalPolicy; }
  [[nodiscard]] Qt::ScrollBarPolicy verticalPolicy() const { return m_verticalPolicy; }
  void setHorizontalArrowButtonsVisible(bool visible);
  void setVerticalArrowButtonsVisible(bool visible);

  void setHorizontalTrailingWidget(QWidget* widget);
  void setVerticalTrailingWidget(QWidget* widget);

  [[nodiscard]] RhiScrollBar* horizontalBar() const { return m_horizontalBar; }
  [[nodiscard]] RhiScrollBar* verticalBar() const { return m_verticalBar; }

  void syncLayout();

private:
  [[nodiscard]] bool shouldShowHorizontalBar() const;
  [[nodiscard]] bool shouldShowVerticalBar() const;

  QAbstractScrollArea* m_area = nullptr;
  ViewportMarginsApplier m_applyViewportMargins;
  RhiScrollBar* m_horizontalBar = nullptr;
  RhiScrollBar* m_verticalBar = nullptr;
  QWidget* m_horizontalTrailingWidget = nullptr;
  QWidget* m_verticalTrailingWidget = nullptr;
  QWidget* m_cornerFill = nullptr;
  Qt::ScrollBarPolicy m_horizontalPolicy = Qt::ScrollBarAlwaysOff;
  Qt::ScrollBarPolicy m_verticalPolicy = Qt::ScrollBarAlwaysOff;
};
