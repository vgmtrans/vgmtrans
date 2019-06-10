/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QObject>

#include <unordered_map>
#include <VGMFile.h>

class VGMFile;

class VGMTreeItem : public QTreeWidgetItem {
   public:
    VGMTreeItem(const QString &name, VGMItem *item, QTreeWidget *parent = nullptr,
                VGMItem *item_parent = nullptr)
        : QTreeWidgetItem(parent, 1001), m_name(name), m_item(item), m_parent(item_parent){};
    auto item_parent() { return m_parent; }
    auto item_offset() { return m_item->dwOffset; }

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
    void addVGMItem(VGMItem *item, VGMItem *parent, const std::wstring &, void *);

   private:
    VGMFile *vgmfile;
    std::unordered_map<VGMItem *, VGMTreeItem *> m_parents;
};
