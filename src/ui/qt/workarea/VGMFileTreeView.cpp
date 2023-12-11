/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMFileTreeView.h"

#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QApplication>
#include <QAccessible>
#include "Helpers.h"

void VGMTreeDisplayItem::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
  QStyleOptionViewItem paintopt = option;
  initStyleOption(&paintopt, index);

  QStyle *style = paintopt.widget ? paintopt.widget->style() : QApplication::style();

  QTextDocument backing_doc;
  backing_doc.setHtml(paintopt.text);

  // Paint the item's background
  paintopt.text = QString{};
  style->drawControl(QStyle::CE_ItemViewItem, &paintopt, painter, paintopt.widget);

  QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &paintopt);
  painter->save();
  painter->translate(textRect.topLeft());
  backing_doc.drawContents(painter, textRect.translated(-textRect.topLeft()));

  painter->restore();
}

QSize VGMTreeDisplayItem::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
  QStyleOptionViewItem style_opt = option;
  initStyleOption(&style_opt, index);

  QTextDocument backing_doc;
  backing_doc.setHtml(style_opt.text);
  backing_doc.setTextWidth(style_opt.rect.width());
  return QSize(backing_doc.idealWidth(), backing_doc.size().height());
}

/*
 * The following is not actually a proper view on the data,
 * but actually an entirely new tree.
 * As long as we need to support the legacy version, this is fine.
 */

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent) : QTreeWidget(parent) {
  setHeaderLabel("File structure");

  /* Items to be added to the top level have their parent set at the vgmfile */
  m_items[file] = invisibleRootItem();
  file->AddToUI(nullptr, this);

  setItemDelegate(new VGMTreeDisplayItem());
}

// Override the focusInEvent to prevent item selection upon focus
void VGMFileTreeView::focusInEvent(QFocusEvent* event) {

}

void VGMFileTreeView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
  // On MacOS, there is a peculiar accessibility-related bug that causes an exception to be thrown here. It causes
  // multiple problems, including issues with tree item selection and a second MDI window not appearing. With no
  // good fix, for now we bypass QTreeView::currentChanged.
#ifdef Q_OS_MAC
  QAbstractItemView::currentChanged(current, previous);
#else
  QTreeView::currentChanged(current, previous);
#endif
}

void VGMFileTreeView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
#ifdef Q_OS_MAC
  QAbstractItemView::selectionChanged(selected, deselected);
#else
  QTreeView::selectionChanged(selected, deselected);
#endif
}

// Find the index to insert a child item, sorted by offset, using binary search
int VGMFileTreeView::getSortedIndex(QTreeWidgetItem* parent, VGMTreeItem* item) {
  int newOffset = item->item_offset();
  int left = 0;
  int right = parent->childCount() - 1;

  while (left <= right) {
    int mid = left + (right - left) / 2;
    VGMTreeItem* childItem = static_cast<VGMTreeItem*>(parent->child(mid));

    if (childItem->item_offset() == newOffset) {
      return mid;
    } else if (childItem->item_offset() < newOffset) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return left;
}


void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &name) {
  auto item_name = QString::fromStdWString(name);
  auto tree_item = new VGMTreeItem(item_name, item, nullptr, parent);
  if (name == item->GetDescription()) {
    tree_item->setText(0, QString{"<b>%1</b><br>Offset: 0x%2 | Length: 0x%3"}
                              .arg(item_name)
                              .arg(item->dwOffset, 1, 16)
                              .arg(item->unLength, 1, 16));
  } else {
    tree_item->setText(0, QString{"<b>%1</b><br>%2<br>Offset: 0x%3 | Length: 0x%4"}
                              .arg(item_name)
                              .arg(QString::fromStdWString(item->GetDescription()))
                              .arg(item->dwOffset, 1, 16)
                              .arg(item->unLength, 1, 16));
  }
  tree_item->setIcon(0, iconForItemType(item->GetIcon()));
  tree_item->setToolTip(0, QString::fromStdWString(item->GetDescription()));

  if (parent != parent_cached) {
    parent_cached = parent;
    parent_item_cached = m_items[parent];
  }

  int insertIndex = getSortedIndex(parent_item_cached, tree_item);
  parent_item_cached->insertChild(insertIndex, tree_item);
  m_items[item] = tree_item;
  tree_item->setData(0, Qt::UserRole, QVariant::fromValue((void *)item));
}