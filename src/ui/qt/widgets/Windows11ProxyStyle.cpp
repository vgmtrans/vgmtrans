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

  return state.testFlag(QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
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

bool isSelectedCustomItemView(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  return viewItem && viewItem->state.testFlag(QStyle::State_Selected) &&
         usesCustomSelectionPanel(widget);
}

QColor selectionTextColor(const QPalette &palette, QPalette::ColorGroup colorGroup) {
  const QColor fillColor = itemSelectionFillColor(palette, colorGroup);
  return contrastingTextColor(fillColor, palette.color(colorGroup, QPalette::Window), palette,
                              colorGroup);
}

void setSelectionTextColors(QPalette &palette, QPalette::ColorGroup colorGroup,
                            const QColor &textColor) {
  palette.setColor(colorGroup, QPalette::HighlightedText, textColor);
  palette.setColor(colorGroup, QPalette::Text, textColor);
  palette.setColor(colorGroup, QPalette::WindowText, textColor);
  palette.setColor(colorGroup, QPalette::ButtonText, textColor);
}

bool isTreeBranchColumn(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  return ancestorWidget<QTreeView>(widget) && viewItem && viewItem->index.isValid() &&
         viewItem->index.column() == 0;
}

QRect selectionBackgroundRect(const QStyleOptionViewItem *viewItem, const QWidget *widget) {
  QRect selectionRect = viewItem->rect;
  if (!isTreeBranchColumn(viewItem, widget)) {
    return selectionRect;
  }

  // Tree items in column 0 need the custom selection fill to extend into the branch gutter.
  const auto *treeView = ancestorWidget<QTreeView>(widget);
  const QWidget *viewport = treeView ? treeView->viewport() : nullptr;
  if (!viewport) {
    return selectionRect;
  }

  if (viewItem->direction == Qt::RightToLeft) {
    selectionRect.setRight(viewport->width() - 1);
  } else {
    selectionRect.setLeft(0);
  }

  return selectionRect;
}

void drawSelectionBackground(QPainter *painter, const QStyleOptionViewItem *viewItem,
                             const QWidget *widget) {
  if (!painter || !viewItem) {
    return;
  }

  const QPalette::ColorGroup colorGroup = colorGroupForState(viewItem->state);
  painter->fillRect(selectionBackgroundRect(viewItem, widget),
                    itemSelectionFillColor(viewItem->palette, colorGroup));
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

  if (element == CE_ItemViewItem && option && painter && widget && usesWindows11BaseStyle(this)) {
    if (const auto *viewItem = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        isSelectedCustomItemView(viewItem, widget)) {
      QStyleOptionViewItem adjustedViewItem(*viewItem);
      const QPalette::ColorGroup colorGroup = colorGroupForState(viewItem->state);
      // Clear Qt's selected state so the Windows 11 base style does not paint its native
      // selection background or accent bar.
      adjustedViewItem.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
      adjustedViewItem.showDecorationSelected = false;
      adjustedViewItem.palette.setBrush(QPalette::Active, QPalette::Accent,
                                        QBrush(kHiddenItemViewAccentColor));
      adjustedViewItem.palette.setBrush(QPalette::Inactive, QPalette::Accent,
                                        QBrush(kHiddenItemViewAccentColor));
      adjustedViewItem.palette.setBrush(QPalette::Disabled, QPalette::Accent,
                                        QBrush(kHiddenItemViewAccentColor));
      setSelectionTextColors(adjustedViewItem.palette, colorGroup,
                             selectionTextColor(adjustedViewItem.palette, colorGroup));
      const CustomSelectionPaintContext previousContext = m_customSelectionPaintContext;
      // Preserve the original selected item so nested primitive paints can still draw our custom
      // background.
      m_customSelectionPaintContext.widget = widget;
      m_customSelectionPaintContext.viewItem = viewItem;
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
    // Draw the selected background ourselves here, because we cleared State_Selected before
    // delegating to the base style.
    const bool usesSelectionContext =
        m_customSelectionPaintContext.widget == widget && m_customSelectionPaintContext.viewItem;
    const QStyleOptionViewItem *viewItem =
        usesSelectionContext ? m_customSelectionPaintContext.viewItem
                             : qstyleoption_cast<const QStyleOptionViewItem *>(option);

    if (viewItem && (usesSelectionContext || isSelectedCustomItemView(viewItem, widget))) {
      if (element == PE_PanelItemViewItem) {
        drawSelectionBackground(painter, viewItem, widget);
      }
      return;
    }
  }

  QProxyStyle::drawPrimitive(element, option, painter, widget);
}
