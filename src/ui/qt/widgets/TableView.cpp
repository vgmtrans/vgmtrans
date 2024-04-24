/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "TableView.h"
#include <QHeaderView>

TableView::TableView(QWidget *parent) : QTableView(parent) {
  setAlternatingRowColors(false);
  setShowGrid(false);
  setSortingEnabled(false);
  setContextMenuPolicy(Qt::CustomContextMenu);
  setWordWrap(false);

  verticalHeader()->hide();

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto header_hor = horizontalHeader();
  connect(header_hor, &QHeaderView::sectionResized, this, &TableView::onHeaderSectionResized);
}

void TableView::setModel(QAbstractItemModel *model) {
  QTableView::setModel(model);

  auto header_hor = horizontalHeader();
  int lastSectionIdx = header_hor->count() - 1;
  header_hor->setSectionsMovable(true);
  header_hor->setHighlightSections(true);
  header_hor->setSectionResizeMode(QHeaderView::Interactive);
  header_hor->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header_hor->setSectionResizeMode(lastSectionIdx, QHeaderView::Fixed);
  header_hor->setMinimumSectionSize(20);
  header_hor->setCascadingSectionResizes(false);
  header_hor->setStyleSheet("QHeaderView::section { padding-left: 6px; }");

  QVariant headerTextVariant = model->headerData(lastSectionIdx, Qt::Horizontal, Qt::DisplayRole);
  if (headerTextVariant.isValid()) {
    QFont headerFont = header_hor->font();
    QFontMetrics fm(headerFont);
    int textWidth = fm.horizontalAdvance(headerTextVariant.toString());
    int columnWidth = std::max(90, textWidth + 15);
    setColumnWidth(lastSectionIdx, columnWidth);
  }
}

void TableView::onHeaderSectionResized(int index, int oldSize, int newSize) {
  // If the lock is set, or the last column was resized, do nothing
  if (headerSectionResizeLock || index + 1 >= horizontalHeader()->count())
    return;

  headerSectionResizeLock = true;

  // Resize the next column to offset the size difference. We want to maintain that the column
  // widths sum to the width of the header
  int difference = newSize - oldSize;
  int nextColWidth = columnWidth(index + 1) - difference;

  int sumOfColWidths = 0;
  for (int i = 0; i < horizontalHeader()->count(); i++) {
    if (i == index + 1)
      sumOfColWidths += nextColWidth;
    else
      sumOfColWidths += columnWidth(i);
  }

  // If the width of all columns will be greater than the header itself, or if the width of any
  // column is less than the minimum allowed, revert. Otherwise, resize.
  if (sumOfColWidths > width() || nextColWidth < horizontalHeader()->minimumSectionSize()) {
    setColumnWidth(index, oldSize);
  } else {
    setColumnWidth(index + 1, columnWidth(index + 1) - difference);
  }

  headerSectionResizeLock = false;
}

void TableView::scrollContentsBy(int dx, int dy) {
  // Call the base class implementation with dx set to 0 to disable horizontal scrolling
  // We disable horizontal scrolling so that we can hide final header column splitter
  QTableView::scrollContentsBy(0, dy);
}

void TableView::resizeEvent(QResizeEvent *event) {
  QAbstractItemView::resizeEvent(event);
  resizeColumns();
}

void TableView::resizeColumns() {
  // Ensure the columns take up the full width of the header.
  // Only the first column should expand/contract
  auto header_hor = horizontalHeader();
  int numCols = header_hor->count();
  int widthOfSecondaryCols = 0;
  for (int i = 1; i < numCols; ++i) {
    widthOfSecondaryCols += columnWidth(i);
  }
  headerSectionResizeLock = true;
  setColumnWidth(0, width() - widthOfSecondaryCols);
  headerSectionResizeLock = false;
}