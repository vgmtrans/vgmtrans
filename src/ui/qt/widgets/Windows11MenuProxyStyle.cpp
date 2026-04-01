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
}

void Windows11MenuProxyStyle::polish(QWidget *widget) {
  QProxyStyle::polish(widget);

  if (auto *menu = qobject_cast<QMenu *>(widget);
      menu && qobject_cast<QGraphicsDropShadowEffect *>(menu->graphicsEffect())) {
    menu->setGraphicsEffect(nullptr);
  }
}

void Windows11MenuProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                            QPainter *painter, const QWidget *widget) const {
  if (element == PE_PanelMenu && option && painter && qobject_cast<const QMenu *>(widget)) {
    QColor borderColor = option->palette.color(QPalette::WindowText);
    borderColor.setAlpha(45);

    auto baseColor = option->palette.color(QPalette::Base);
    auto alternateBaseColor = option->palette.color(QPalette::AlternateBase);
    auto bgColor = blendColors(baseColor, alternateBaseColor, 0.9);
    bgColor.setAlpha(255);

    painter->setPen(borderColor);
    painter->setBrush(bgColor);
    painter->drawRoundedRect(option->rect.marginsRemoved(QMargins(2, 2, 2, 2)),
                             kWindows11MenuCornerRadius, kWindows11MenuCornerRadius);
    return;
  }

  QProxyStyle::drawPrimitive(element, option, painter, widget);
}
