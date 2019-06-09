/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMFileTreeView.h"

#include "../QtVGMRoot.h"
#include "../util/Helpers.h"

#include <VGMFile.h>
#include <VGMItem.h>

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent)
    : QTreeWidget(parent), vgmfile(file) {
    setHeaderLabel("File structure");

    /* Not pretty, but there's no alternative for now */
    connect(&qtVGMRoot, &QtVGMRoot::UI_AddItem, this, &VGMFileTreeView::addVGMItem);
    file->AddToUI(nullptr, nullptr);
    disconnect(&qtVGMRoot, &QtVGMRoot::UI_AddItem, this, &VGMFileTreeView::addVGMItem);
}

void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &name, void *) {
    auto item_name = QString::fromStdWString(item->name);
    auto *treeItem = new VGMTreeItem(item_name, item, nullptr, parent);
    treeItem->setText(0, item_name);
    treeItem->setIcon(0, iconForItemType(item->GetIcon()));

    static VGMTreeItem *parent_cache = nullptr;

    if (parent) {
            if(parent_cache && parent_cache->item_parent() == parent) {
                parent_cache->addChild(treeItem);
            } else {
                auto *item_app = m_parents[parent];
                if (item_app) {
                    item_app->addChild(treeItem);
                    parent_cache = item_app;
                } else {
                    /* We have this, sometimes */
                    addTopLevelItem(treeItem);
                }
            }
    } else {
        addTopLevelItem(treeItem);
        m_parents[item] = treeItem;
    }
}
