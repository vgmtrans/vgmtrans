/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMFileTreeView.h"

#include <VGMFile.h>
#include <VGMItem.h>
#include "QtVGMRoot.h"
#include "Helpers.h"

/*
* The following is not actually a proper view on the data,
* but actually an entirely new tree.
* As long as we need to support the legacy version, this is fine.
*/

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent)
    : QTreeWidget(parent) {
  setHeaderLabel("File structure");

  /* Items to be added to the top level have their parent set at the vgmfile */
  m_items[file] = invisibleRootItem();
  file->AddToUI(nullptr, this);
}

void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &name) {
  auto item_name = QString::fromStdWString(name);
  auto tree_item = new VGMTreeItem(item_name, item, nullptr, parent);
  tree_item->setText(0, item_name);
  tree_item->setIcon(0, iconForItemType(item->GetIcon()));

  if(parent != parent_cached) {
    parent_cached = parent;
    parent_item_cached = m_items[parent];
  }
  
  parent_item_cached->addChild(tree_item);
  m_items[item] = tree_item;
  tree_item->setData(0, Qt::UserRole, QVariant::fromValue((void *)item));
}