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
    parent_cache = nullptr;
    disconnect(&qtVGMRoot, &QtVGMRoot::UI_AddItem, this, &VGMFileTreeView::addVGMItem);
}

void VGMFileTreeView::addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &name, void *) {
    auto item_name = QString::fromStdWString(name);
    auto tree_item = std::make_unique<VGMTreeItem>(item_name, item, nullptr, parent);
    tree_item->setText(0, item_name);
    tree_item->setIcon(0, iconForItemType(item->GetIcon()));

    if (parent) {
        if (parent_cache && parent_cache->item_parent() == parent) {
            parent_cache->addChild(tree_item.get());
        } else {
            auto item_app = m_parents[parent].get();
            if (item_app) {
                item_app->addChild(tree_item.get());
                parent_cache = item_app;
            } else {
                /* We have this, sometimes */
                addTopLevelItem(tree_item.get());
            }
        }
    } else {
        addTopLevelItem(tree_item.get());
        m_parents.insert(std::make_pair(item, std::move(tree_item)));
    }
}
