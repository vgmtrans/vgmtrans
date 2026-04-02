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

namespace {
constexpr int kWindows11MenuCornerRadius = 8;
constexpr int kWindows11MenuItemHorizontalPadding = 6;
constexpr int kWindows11MenuItemVerticalPadding = 2;
constexpr int kItemSelectionTextInset = 4;
constexpr int kTableItemSelectionTextInset = 0;
constexpr int kTableSelectionIndicatorWidth = 3;
constexpr int kTableSelectionIndicatorGap = 4;
constexpr int kTableSelectionIndicatorVerticalInset = 4;

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
  return ancestorWidget<QTableView>(widget) || ancestorWidget<QListView>(widget);
}

bool isLeadingTableCell(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  const auto *tableView = ancestorWidget<QTableView>(widget);
  if (!tableView || !viewItem) {
    return false;
  }

  const QWidget *viewport = tableView->viewport();
  if (!viewport) {
    return false;
  }

  const int leadingEdgeX = viewItem->direction == Qt::RightToLeft ? viewport->width() - 1 : 0;
  const int leadingColumn = tableView->columnAt(leadingEdgeX);
  const int itemColumn = tableView->columnAt(viewItem->rect.center().x());
  return leadingColumn >= 0 && itemColumn == leadingColumn;
}

bool isLeadingSelectedTableCell(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  return isLeadingTableCell(viewItem, widget) &&
         viewItem && viewItem->state.testFlag(QStyle::State_Selected);
}

int tableSelectionContentOffset(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  return isLeadingTableCell(viewItem, widget)
             ? kTableSelectionIndicatorWidth + kTableSelectionIndicatorGap
             : 0;
}

QRect tableSelectionIndicatorRect(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  if (!isLeadingSelectedTableCell(viewItem, widget)) {
    return {};
  }

  QRect indicatorRect = viewItem->rect;
  if (viewItem->direction == Qt::RightToLeft) {
    indicatorRect.setLeft(std::max(indicatorRect.left(),
                                   indicatorRect.right() - kTableSelectionIndicatorWidth + 1));
  } else {
    indicatorRect.setRight(std::min(indicatorRect.right(),
                                    indicatorRect.left() + kTableSelectionIndicatorWidth - 1));
  }

  indicatorRect.adjust(0, kTableSelectionIndicatorVerticalInset, 0,
                       -kTableSelectionIndicatorVerticalInset);
  return indicatorRect;
}

QRect selectionFillRect(const QStyleOptionViewItem *viewItem, const QStyle *style,
                        const QWidget *widget) {
  if (!viewItem) {
    return {};
  }

  QRect fillRect = viewItem->rect;
  if (!viewItem->features.testFlag(QStyleOptionViewItem::HasDecoration) ||
      !viewItem->decorationSize.isValid() || !style) {
    return fillRect;
  }

  const int textInset = ancestorWidget<QTableView>(widget) ? kTableItemSelectionTextInset
                                                            : kItemSelectionTextInset;
  const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, viewItem, widget);
  if (textRect.isValid()) {
    if (viewItem->direction == Qt::RightToLeft) {
      fillRect.setRight(std::min(fillRect.right(), textRect.right() + textInset));
    } else {
      fillRect.setLeft(std::max(fillRect.left(), textRect.left() - textInset));
    }
    return fillRect;
  }

  if (viewItem->direction == Qt::RightToLeft) {
    fillRect.adjust(0, 0, -(viewItem->decorationSize.width() + textInset), 0);
  } else {
    fillRect.adjust(viewItem->decorationSize.width() + textInset, 0, 0, 0);
  }
  return fillRect;
}
}

void Windows11ProxyStyle::polish(QWidget *widget) {
  QProxyStyle::polish(widget);

  if (auto *menu = qobject_cast<QMenu *>(widget);
      menu && qobject_cast<QGraphicsDropShadowEffect *>(menu->graphicsEffect())) {
    menu->setGraphicsEffect(nullptr);
  }
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

  QProxyStyle::drawControl(element, option, painter, widget);
}

QRect Windows11ProxyStyle::subElementRect(SubElement element, const QStyleOption *option,
                                          const QWidget *widget) const {
  QRect rect = QProxyStyle::subElementRect(element, option, widget);
  if (!widget || !usesWindows11BaseStyle(this)) {
    return rect;
  }

  if (element != SE_ItemViewItemCheckIndicator && element != SE_ItemViewItemDecoration &&
      element != SE_ItemViewItemText && element != SE_ItemViewItemFocusRect) {
    return rect;
  }

  const auto *viewItem = qstyleoption_cast<const QStyleOptionViewItem *>(option);
  const int contentOffset = tableSelectionContentOffset(viewItem, widget);
  if (contentOffset <= 0) {
    return rect;
  }

  rect.translate(viewItem->direction == Qt::RightToLeft ? -contentOffset : contentOffset, 0);
  return rect;
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

  if ((element == PE_PanelItemViewRow || element == PE_PanelItemViewItem) && option && painter &&
      widget && usesWindows11BaseStyle(this)) {
    if (const auto *viewItem = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        viewItem && viewItem->state.testFlag(QStyle::State_Selected) &&
        usesCustomSelectionPanel(widget)) {
      if (ancestorWidget<QTableView>(widget) && element == PE_PanelItemViewRow) {
        return;
      }

      // Qt's Windows 11 style paints rounded selection chrome per table cell, which shows up as
      // narrow leading bars in our selected item views. Fill the row/item panel directly so
      // selected tables and lists keep a normal continuous highlight.
      painter->fillRect(selectionFillRect(viewItem, this, widget),
                        viewItem->palette.brush(colorGroupForState(viewItem->state),
                                                QPalette::Highlight));

      if (const QRect indicatorRect = tableSelectionIndicatorRect(viewItem, widget);
          indicatorRect.isValid()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(viewItem->palette.brush(colorGroupForState(viewItem->state),
                                                  QPalette::Accent));
        painter->drawRoundedRect(indicatorRect, indicatorRect.width() / 2.0,
                                 indicatorRect.width() / 2.0);
        painter->restore();
      }

      return;
    }
  }

  QProxyStyle::drawPrimitive(element, option, painter, widget);
}
