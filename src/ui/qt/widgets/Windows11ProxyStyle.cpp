/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "Windows11ProxyStyle.h"
#include "UIHelpers.h"

#include <QGraphicsDropShadowEffect>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QStyleOption>
#include <QTableView>
#include <QTreeView>

namespace {
constexpr int kWindows11MenuCornerRadius = 8;
constexpr int kWindows11MenuItemHorizontalPadding = 6;
constexpr int kWindows11MenuItemVerticalPadding = 2;
constexpr int kItemSelectionAccentAlpha = 64;
const QColor kHiddenItemViewAccentColor(Qt::transparent);

QColor menuBackgroundColor(const QPalette &palette) {
  QColor backgroundColor = blendColors(palette.color(QPalette::Base),
                                       palette.color(QPalette::AlternateBase), 0.9);
  backgroundColor.setAlpha(255);
  return backgroundColor;
}

template <typename T>
const T *ancestorWidget(const QWidget *widget) {
  const QWidget *current = widget;
  while (current) {
    if (const auto *matchedWidget = qobject_cast<const T *>(current)) {
      return matchedWidget;
    }
    current = current->parentWidget();
  }
  return nullptr;
}

QPalette::ColorGroup colorGroupForState(QStyle::State state) {
  if (!state.testFlag(QStyle::State_Enabled)) {
    return QPalette::Disabled;
  }

  return state.testFlag(QStyle::State_Active) ? QPalette::Normal : QPalette::Inactive;
}

bool usesWindows11BaseStyle(const QProxyStyle *style) {
  const QStyle *baseStyle = style ? style->baseStyle() : nullptr;
  return baseStyle &&
         (baseStyle->inherits("QWindows11Style") ||
          baseStyle->name().compare(QStringLiteral("windows11"), Qt::CaseInsensitive) == 0);
}

bool usesCustomSelectionPanel(const QWidget *widget) {
  return ancestorWidget<QTableView>(widget) || ancestorWidget<QListView>(widget) ||
         ancestorWidget<QTreeView>(widget);
}

bool isLeadingTableCell(const QStyleOptionViewItem *viewItem, const QWidget *widget);

void setAccentBrush(QPalette &palette, const QBrush &brush) {
  palette.setBrush(QPalette::Active, QPalette::Accent, brush);
  palette.setBrush(QPalette::Inactive, QPalette::Accent, brush);
  palette.setBrush(QPalette::Disabled, QPalette::Accent, brush);
}

void setHighlightedTextColors(QPalette &palette) {
  for (const auto colorGroup : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    const QColor highlightedTextColor = palette.color(colorGroup, QPalette::HighlightedText);
    palette.setColor(colorGroup, QPalette::Text, highlightedTextColor);
    palette.setColor(colorGroup, QPalette::WindowText, highlightedTextColor);
    palette.setColor(colorGroup, QPalette::ButtonText, highlightedTextColor);
  }
}

QBrush accentBrush(const QStyleOptionViewItem *viewItem) {
  if (!viewItem) {
    return {};
  }

  return viewItem->palette.brush(colorGroupForState(viewItem->state), QPalette::Accent);
}

QBrush selectionFillBrush(const QStyleOptionViewItem *viewItem) {
  QColor accentColor = accentBrush(viewItem).color();
  accentColor.setAlpha(kItemSelectionAccentAlpha);
  return QBrush(accentColor);
}

void drawSelectionBackground(QPainter *painter, const QStyleOptionViewItem *viewItem,
                             const QWidget *widget) {
  if (!painter || !viewItem) {
    return;
  }

  painter->fillRect(viewItem->rect, selectionFillBrush(viewItem));
}
}

void Windows11ProxyStyle::polish(QWidget *widget) {
  QProxyStyle::polish(widget);

  if (auto *menu = qobject_cast<QMenu *>(widget);
      menu && qobject_cast<QGraphicsDropShadowEffect *>(menu->graphicsEffect())) {
    menu->setGraphicsEffect(nullptr);
  }
}

const QStyleOptionViewItem *Windows11ProxyStyle::currentCustomSelectionViewItem(
    const QWidget *widget) const {
  return m_customSelectionPaintContext.active && m_customSelectionPaintContext.widget == widget
             ? &m_customSelectionPaintContext.viewItem
             : nullptr;
}

QSize Windows11ProxyStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                            const QSize &size, const QWidget *widget) const {
  QSize contentSize = QProxyStyle::sizeFromContents(type, option, size, widget);

  if (type != CT_MenuItem || !qobject_cast<const QMenu *>(widget)) {
    return contentSize;
  }

  const auto *menuItem = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
  if (!menuItem || menuItem->menuItemType == QStyleOptionMenuItem::Separator) {
    return contentSize;
  }

  contentSize.rwidth() += kWindows11MenuItemHorizontalPadding * 2;
  contentSize.rheight() += kWindows11MenuItemVerticalPadding * 2;
  return contentSize;
}

void Windows11ProxyStyle::drawControl(ControlElement element, const QStyleOption *option,
                                      QPainter *painter, const QWidget *widget) const {
  if (element == CE_MenuItem && option && painter && qobject_cast<const QMenu *>(widget)) {
    if (const auto *menuItem = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
      QStyleOptionMenuItem paddedMenuItem(*menuItem);
      paddedMenuItem.rect = menuItem->rect.marginsRemoved(
          QMargins(kWindows11MenuItemHorizontalPadding, 0,
                   kWindows11MenuItemHorizontalPadding, 0));
      QProxyStyle::drawControl(element, &paddedMenuItem, painter, widget);
      return;
    }
  }

  if (element == CE_ItemViewItem && option && painter && widget && usesWindows11BaseStyle(this)) {
    if (const auto *viewItem = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        viewItem && viewItem->state.testFlag(QStyle::State_Selected) &&
        usesCustomSelectionPanel(widget)) {
      QStyleOptionViewItem adjustedViewItem(*viewItem);
      adjustedViewItem.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
      adjustedViewItem.showDecorationSelected = false;
      setAccentBrush(adjustedViewItem.palette, QBrush(kHiddenItemViewAccentColor));
      setHighlightedTextColors(adjustedViewItem.palette);
      const CustomSelectionPaintContext previousContext = m_customSelectionPaintContext;
      m_customSelectionPaintContext.widget = widget;
      m_customSelectionPaintContext.viewItem = *viewItem;
      m_customSelectionPaintContext.active = true;
      QProxyStyle::drawControl(element, &adjustedViewItem, painter, widget);
      m_customSelectionPaintContext = previousContext;
      return;
    }
  }

  QProxyStyle::drawControl(element, option, painter, widget);
}

void Windows11ProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
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

  if ((element == PE_PanelItemViewRow || element == PE_PanelItemViewItem) && painter && widget &&
      usesWindows11BaseStyle(this)) {
    if (const QStyleOptionViewItem *contextViewItem = currentCustomSelectionViewItem(widget)) {
      if (element == PE_PanelItemViewRow) {
        return;
      }

      drawSelectionBackground(painter, contextViewItem, widget);
      return;
    }

    if (const auto *viewItem = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        viewItem && viewItem->state.testFlag(QStyle::State_Selected) &&
        usesCustomSelectionPanel(widget)) {
      if (element == PE_PanelItemViewRow) {
        return;
      }

      drawSelectionBackground(painter, viewItem, widget);
      return;
    }
  }

  QProxyStyle::drawPrimitive(element, option, painter, widget);
}
