/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QObject>

#include <unordered_map>
#include <utility>
#include <VGMFile.h>

class VGMFile;

class VGMTreeItem : public QTreeWidgetItem {
   public:
    VGMTreeItem(QString name, VGMItem *item, QTreeWidget *parent = nullptr,
                VGMItem *item_parent = nullptr)
        : QTreeWidgetItem(parent, 1001), m_name(std::move(name)), m_item(item), m_parent(item_parent){};
    ~VGMTreeItem() override = default;

    [[nodiscard]] inline auto item_parent() noexcept { return m_parent; }
    [[nodiscard]] inline auto item_offset() noexcept { return m_item->dwOffset; }

   private:
    QString m_name;
    VGMItem *m_item = nullptr;
    VGMItem *m_parent = nullptr;
};

class VGMFileTreeView : public QTreeWidget {
    Q_OBJECT

   public:
    explicit VGMFileTreeView(VGMFile *vgmfile, QWidget *parent = nullptr);
    ~VGMFileTreeView() override = default;

   public slots:
    void addVGMItem(VGMItem *item, VGMItem *parent, const std::string &, void *);

   private:
    VGMFile *vgmfile;
    std::unordered_map<VGMItem *, VGMTreeItem *> m_parents;

    VGMTreeItem *parent_cache = nullptr;
};
