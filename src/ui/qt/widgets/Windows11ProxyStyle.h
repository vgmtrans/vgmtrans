/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QProxyStyle>

class QWidget;
class QPainter;
class QStyleOption;

class Windows11ProxyStyle final : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  void polish(QWidget *widget) override;
  QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                         const QWidget *widget) const override;
  void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                   const QWidget *widget = nullptr) const override;
  QRect subElementRect(SubElement element, const QStyleOption *option,
                       const QWidget *widget = nullptr) const override;
  void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
};
