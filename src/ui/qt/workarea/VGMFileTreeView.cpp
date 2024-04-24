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
#include <QScrollBar>
#include <QCheckBox>
#include "Helpers.h"
#include "Metrics.h"
#include "services/NotificationCenter.h"
#include "services/Settings.h"

// ***********************************
// VMGFileTreeHeaderView
// ***********************************

VMGFileTreeHeaderView::VMGFileTreeHeaderView(Qt::Orientation orientation, QWidget *parent, bool showDetails)
    : QHeaderView(orientation, parent) {
  setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setSectionResizeMode(QHeaderView::Fixed);

  detailsCheckBox = new QCheckBox("Show Details", this);
  detailsCheckBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  detailsCheckBox->setStyleSheet(
      QString("QCheckBox::indicator { width: %1px; height: %1px; }").arg(Size::HeaderCheckbox));
  detailsCheckBox->setChecked(showDetails);

  detailsCheckBox->show();

  connect(detailsCheckBox, &QCheckBox::clicked, this, &VMGFileTreeHeaderView::toggleShowDetails);
  connect(NotificationCenter::the(), &NotificationCenter::vgmfiletree_showDetailsChanged,
          this, &VMGFileTreeHeaderView::onShowDetailsChanged);
}

// MacOS exhibits strange behavior around the QHeaderView font. The font is not correctly set when
// accessed in the constructor, so we delay setting to showEvent. What's more, the font size will
// still be incorrect until we set it with setPointSize. In our case, we're shrinking it anyway.
void VMGFileTreeHeaderView::showEvent(QShowEvent *event) {
  QFont headerFont = font();
  headerFont.setPointSize(headerFont.pointSize()-1);
  detailsCheckBox->setFont(headerFont);

  QHeaderView::showEvent(event);
}

void VMGFileTreeHeaderView::resizeEvent(QResizeEvent *event) {
  QHeaderView::resizeEvent(event);

  // Resize the first and only section to 1 pixel beyond the width to hide the column splitter
  resizeSection(0, width() + 1);
  detailsCheckBox->move(width() - detailsCheckBox->width() - 10,
                    (height() - detailsCheckBox->height()) / 2);
}

void VMGFileTreeHeaderView::onShowDetailsChanged(bool showDetails) {
  detailsCheckBox->setChecked(showDetails);
}

void VMGFileTreeHeaderView::toggleShowDetails() {
  Settings::the()->VGMFileTreeView.setShowDetails(detailsCheckBox->isChecked());
}

// ***********************************
// VGMTreeDisplayItem
// ***********************************

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


// ***********************************
// VGMFileTreeView
// ***********************************

/*
 * The following is not actually a proper view on the data,
 * but actually an entirely new tree.
 * As long as we need to support the legacy version, this is fine.
 */


VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent) : QTreeWidget(parent) {
  setHeaderLabel("File structure");

  // Load persistent settings
  showDetails = Settings::the()->VGMFileTreeView.showDetails();

  // Items to be added to the top level have their parent set at the vgmfile
  m_items[file] = invisibleRootItem();
  file->AddToUI(nullptr, this);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  horizontalScrollBar()->setEnabled(false);

  VMGFileTreeHeaderView *headerView = new VMGFileTreeHeaderView(Qt::Horizontal, this, showDetails);
  this->setHeader(headerView);

  setItemDelegate(new VGMTreeDisplayItem());

  connect(NotificationCenter::the(), &NotificationCenter::vgmfiletree_showDetailsChanged,
          this, &VGMFileTreeView::onShowDetailsChanged);
}

void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::string &name) {
  auto item_name = QString::fromStdString(name);
  auto tree_item = new VGMTreeItem(item_name, item, nullptr, parent);

  setItemText(item, tree_item);

  if (parent != parent_cached) {
    parent_cached = parent;
    parent_item_cached = m_items[parent];
  }

  int insertIndex = getSortedIndex(parent_item_cached, tree_item);
  parent_item_cached->insertChild(insertIndex, tree_item);
  m_items[item] = tree_item;
  m_treeItemToVGMItem[tree_item] = item;
  tree_item->setData(0, Qt::UserRole, QVariant::fromValue((void *)item));
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

  updateStatusBar();
}

void VGMFileTreeView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
#ifdef Q_OS_MAC
  QAbstractItemView::selectionChanged(selected, deselected);
#else
  QTreeView::selectionChanged(selected, deselected);
#endif
}

void VGMFileTreeView::mousePressEvent(QMouseEvent *event) {
  // Get the item at the current mouse position
  QTreeWidgetItem *itemAtPoint = itemAt(event->pos());

  // If the item at the mouse position is already selected
  if (itemAtPoint && itemAtPoint->isSelected()) {
    clearSelection();
    setCurrentItem(nullptr);
  } else {
    QTreeWidget::mousePressEvent(event);
  }
}

void VGMFileTreeView::mouseDoubleClickEvent(QMouseEvent *event) {
  QTreeWidgetItem *itemAtPoint = itemAt(event->pos());

  // Check if the item is expandable/contractible
  if (itemAtPoint && itemAtPoint->childCount() > 0) {
    // If it's expandable, forward the event to the default behavior
    QTreeWidget::mouseDoubleClickEvent(event);
  } else {
    // If not, treat the second click like a normal mousePressEvent
    mousePressEvent(event);
  }
}

void VGMFileTreeView::scrollContentsBy(int dx, int dy) {
  // Call the base class implementation with dx set to 0 to disable horizontal scrolling
  // We disable horizontal scrolling so that we can hide the header column splitter
  QTreeWidget::scrollContentsBy(0, dy);
}

void VGMFileTreeView::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Left) {
    QTreeWidgetItem *current = currentItem();

    // If the item has a parent and is not expanded, move to the parent
    if (current && current->parent() && !isExpanded(indexFromItem(current))) {
      QTreeWidgetItem *parent = current->parent();
      setCurrentItem(parent);
      return;
    }
  }

  // Call base class keyPressEvent for other keys and unhandled cases
  QTreeWidget::keyPressEvent(event);
}

// Update the status bar for the current selection
void VGMFileTreeView::updateStatusBar() {
  QTreeWidgetItem *treeItem = currentItem();
  if (!treeItem) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }

  VGMItem* vgmItem = m_treeItemToVGMItem[treeItem];
  if (!vgmItem) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }

  NotificationCenter::the()->updateStatusForItem(vgmItem);
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

void VGMFileTreeView::setItemText(VGMItem* item, VGMTreeItem* treeItem) {
  auto name = QString::fromStdString(item->name);
  if (showDetails) {
    if (item->GetDescription().empty()) {
      treeItem->setText(0, QString{"<b>%1</b><br>Offset: 0x%2 | Length: 0x%3"}
                               .arg(name)
                               .arg(item->dwOffset, 1, 16)
                               .arg(item->unLength, 1, 16));
    } else {
      treeItem->setText(0, QString{"<b>%1</b><br>%2<br>Offset: 0x%3 | Length: 0x%4"}
                               .arg(name)
                               .arg(QString::fromStdString(item->GetDescription()))
                               .arg(item->dwOffset, 1, 16)
                               .arg(item->unLength, 1, 16));
    }
  } else {
    treeItem->setText(0, name);
  }
  treeItem->setIcon(0, iconForItemType(item->GetIcon()));
  treeItem->setToolTip(0, QString::fromStdString(item->GetDescription()));
}

void VGMFileTreeView::onShowDetailsChanged(bool show) {
  this->showDetails = show;

  // We will update the text and icons of all items. We temporarily disable the model from emitting
  // signals as this causes performance issues on MacOS and Windows due to unnecessary drawing
  model()->blockSignals(true);
  updateItemTextRecursively(invisibleRootItem());
  model()->blockSignals(false);

  doItemsLayout();
  scrollToItem(currentItem());
}

void VGMFileTreeView::updateItemTextRecursively(QTreeWidgetItem* item) {
  if (!item) return;

  VGMTreeItem* vgmTreeItem = static_cast<VGMTreeItem*>(item);
  VGMItem* vgmitem = m_treeItemToVGMItem[item];

  if (vgmitem) {
    setItemText(vgmitem, vgmTreeItem);
  }

  // Recursively update children
  for (int i = 0; i < item->childCount(); ++i) {
    updateItemTextRecursively(item->child(i));
  }
}