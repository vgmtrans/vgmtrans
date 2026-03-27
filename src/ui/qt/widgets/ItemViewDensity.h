/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

class QAbstractItemView;
class QListView;
class QTableView;

namespace ItemViewDensity {
int textHeight(const QAbstractItemView* view);
int contentHeight(const QAbstractItemView* view);
int tableRowHeight(const QTableView* view);
int listItemHeight(const QListView* view);
int listSpacing(const QListView* view);
int listItemStride(const QListView* view);
void apply(QTableView* view);
void apply(QListView* view);
}
