/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "Windows11MenuProxyStyle.h"
#include "UIHelpers.h"

#include <QGraphicsDropShadowEffect>
#include <QMenu>
#include <QPainter>
#include <QStyleOption>

namespace {
constexpr int kWindows11MenuCornerRadius = 8;
constexpr int kWindows11MenuItemVerticalPadding = 2;

QColor menuBackgroundColor(const QPalette &palette) {
  QColor backgroundColor = blendColors(palette.color(QPalette::Base),
                                       palette.color(QPalette::AlternateBase), 0.9);
  backgroundColor.setAlpha(255);
  return backgroundColor;
}
}

void Windows11MenuProxyStyle::polish(QWidget *widget) {
  QProxyStyle::polish(widget);

  if (auto *menu = qobject_cast<QMenu *>(widget);
      menu && qobject_cast<QGraphicsDropShadowEffect *>(menu->graphicsEffect())) {
    menu->setGraphicsEffect(nullptr);
  }
}

QSize Windows11MenuProxyStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                                const QSize &size, const QWidget *widget) const {
  QSize contentSize = QProxyStyle::sizeFromContents(type, option, size, widget);

  if (type != CT_MenuItem || !qobject_cast<const QMenu *>(widget)) {
    return contentSize;
  }

  const auto *menuItem = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
  if (!menuItem || menuItem->menuItemType == QStyleOptionMenuItem::Separator) {
    return contentSize;
  }

  contentSize.rheight() += kWindows11MenuItemVerticalPadding * 2;
  return contentSize;
}

void Windows11MenuProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                            QPainter *painter, const QWidget *widget) const {
  if (element == PE_PanelMenu && option && painter && qobject_cast<const QMenu *>(widget)) {
    QColor borderColor = option->palette.color(QPalette::WindowText);
    borderColor.setAlpha(45);

    painter->setPen(borderColor);
    painter->setBrush(menuBackgroundColor(option->palette));
    painter->drawRoundedRect(option->rect.marginsRemoved(QMargins(2, 2, 2, 2)),
                             kWindows11MenuCornerRadius, kWindows11MenuCornerRadius);
    return;
  }

  QProxyStyle::drawPrimitive(element, option, painter, widget);
}
