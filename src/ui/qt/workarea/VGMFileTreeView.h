/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStyledItemDelegate>
#include <QHeaderView>
#include <QBrush>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "VGMFile.h"

class VGMFile;
class QCheckBox;

// ***********************************
// VMGFileTreeHeaderView
// ***********************************

class VMGFileTreeHeaderView : public QHeaderView {
  Q_OBJECT

public:
  VMGFileTreeHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr, bool showDetails = false);

private:
  QCheckBox* detailsCheckBox;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void onShowDetailsChanged(bool showDetails) const;
  void toggleShowDetails() const;
};

// ***********************************
// VGMTreeItem
// ***********************************

class VGMTreeItem : public QTreeWidgetItem {
  static constexpr auto ItemType = QTreeWidgetItem::UserType + 1;

public:
  VGMTreeItem(QString name, VGMItem *item, QTreeWidget *parent = nullptr,
              VGMItem *item_parent = nullptr)
      : QTreeWidgetItem(parent, ItemType), m_name(std::move(name)), m_item(item),
        m_parent(item_parent){};
  ~VGMTreeItem() override = default;

  [[nodiscard]] inline auto item_parent() const noexcept { return m_parent; }
  [[nodiscard]] inline auto item_offset() const noexcept { return m_item->offset(); }
  [[nodiscard]] inline auto item() const noexcept { return m_item; }

private:
  QString m_name;
  VGMItem *m_item = nullptr;
  VGMItem *m_parent = nullptr;
};

// ***********************************
// VGMTreeDisplayItem
// ***********************************

class VGMTreeDisplayItem : public QStyledItemDelegate {
  Q_OBJECT
public:
  explicit VGMTreeDisplayItem(QObject *parent = nullptr)
      : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// ***********************************
// VGMFileTreeView
// ***********************************

class VGMFileTreeView : public QTreeWidget {
  Q_OBJECT
public:
  explicit VGMFileTreeView(VGMFile *vgmfile, QWidget *parent = nullptr);
  ~VGMFileTreeView() override = default;

  void addVGMItem(VGMItem *item, VGMItem *parent, const std::string &name);
  auto getTreeWidgetItem(const VGMItem *vgm_item) const { return m_items.at(vgm_item); };
  void updateStatusBar();
  void setPlaybackItems(const std::vector<const VGMItem*>& items);

protected:
  void focusInEvent(QFocusEvent* event) override;
  void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;

private:
  static int getSortedIndex(const QTreeWidgetItem* parent, const VGMTreeItem* item);
  void setItemText(VGMItem* item, VGMTreeItem* treeItem) const;
  void onShowDetailsChanged(bool showDetails);
  void updateItemTextRecursively(QTreeWidgetItem* item);
  void seekToTreeItem(QTreeWidgetItem* item, bool allowRepeat = false);

  bool showDetails = false;
  QTreeWidgetItem *parent_item_cached{};
  VGMItem *parent_cached{};
  std::unordered_map<const VGMItem*, QTreeWidgetItem*> m_items{};
  std::unordered_map<QTreeWidgetItem*, VGMItem*> m_treeItemToVGMItem{};
  QTreeWidgetItem* m_lastSeekItem{};
  std::unordered_set<QTreeWidgetItem*> m_playbackTreeItems{};
  QBrush m_playbackBrush{};
};
