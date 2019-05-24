/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QObject>

#include <map>
//#include "VGMFileItemModel.h"

class VGMFile;
class VGMItem;

class VGMTreeItem : public QTreeWidgetItem {
    public:
     VGMTreeItem(QString name, VGMItem *item, QTreeWidget *parent = nullptr, VGMItem *item_parent = nullptr) : QTreeWidgetItem(parent, 1001), m_name(name), m_item(item), m_parent(item_parent) {};
     auto item_parent() { return m_parent; }

    private:
        QString m_name;
        VGMItem *m_item;
        VGMItem *m_parent;
};

class VGMFileTreeView : public QTreeWidget {
    Q_OBJECT

   public:
    VGMFileTreeView(VGMFile *vgmfile, QWidget *parent = nullptr);

public slots:
    void addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &, void*);

   private:
    VGMFile *vgmfile;
    std::map<VGMItem *, VGMTreeItem*> m_parents;
    //VGMFileItemModel model;
};
