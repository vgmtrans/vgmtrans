/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "ItemViewDensity.h"

#include <algorithm>
#include <cmath>

#include <QAbstractItemView>
#include <QHeaderView>
#include <QListView>
#include <QTableView>

namespace {
constexpr double kTablePaddingRatio = 0.78;
constexpr int kTablePaddingMin = 4;
constexpr double kListPaddingRatio = 0.22;
constexpr int kListPaddingMin = 2;
constexpr double kListSpacingRatio = 0.03;

int scaledPadding(int textHeight, double ratio, int minimum) {
  return std::max(minimum, static_cast<int>(std::lround(textHeight * ratio)));
}
}

namespace ItemViewDensity {
int textHeight(const QAbstractItemView* view) {
  return view ? view->fontMetrics().lineSpacing() : 0;
}

int contentHeight(const QAbstractItemView* view) {
  if (!view) {
    return 0;
  }

  const QSize iconSize = view->iconSize();
  const int iconHeight = iconSize.isValid() ? iconSize.height() : 0;
  return std::max(textHeight(view), iconHeight);
}

int tableRowHeight(const QTableView* view) {
  return contentHeight(view) + scaledPadding(textHeight(view), kTablePaddingRatio, kTablePaddingMin);
}

int listItemHeight(const QListView* view) {
  return contentHeight(view) + scaledPadding(textHeight(view), kListPaddingRatio, kListPaddingMin);
}

int listSpacing(const QListView* view) {
  if (!view) {
    return 0;
  }

  return std::max(0, static_cast<int>(std::lround(textHeight(view) * kListSpacingRatio)));
}

int listItemStride(const QListView* view) {
  return listItemHeight(view) + listSpacing(view);
}

void apply(QTableView* view) {
  if (!view) {
    return;
  }

  view->verticalHeader()->setDefaultSectionSize(tableRowHeight(view));
}

void apply(QListView* view) {
  if (!view) {
    return;
  }

  view->setSpacing(listSpacing(view));
}
}
