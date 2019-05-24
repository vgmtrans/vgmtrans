/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMFileTreeView.h"

#include "../QtVGMRoot.h"

#include <VGMFile.h>
#include <VGMItem.h>

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent)
    : QTreeWidget(parent), vgmfile(file) {  //}, model(file) {

    connect(&qtVGMRoot, &QtVGMRoot::UI_AddItem, this, &VGMFileTreeView::addVGMItem);
    file->AddToUI(nullptr, nullptr);
}

void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &, void *) {
    auto *treeItem = new VGMTreeItem(QString::fromStdWString(item->name), item, nullptr, parent);
    treeItem->setText(0, QString::fromStdWString(item->name));

    if (parent) {
        m_parents[parent]->addChild(treeItem);
    } else {
        addTopLevelItem(treeItem);
        m_parents[item] = treeItem;
    }
}
