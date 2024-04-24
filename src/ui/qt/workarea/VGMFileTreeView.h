/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QObject>
#include <QStyledItemDelegate>
#include <QHeaderView>
#include <unordered_map>
#include <utility>
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
  void resizeEvent(QResizeEvent *event);
  void showEvent(QShowEvent *event);
  void onShowDetailsChanged(bool showDetails);
  void toggleShowDetails();
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

  [[nodiscard]] inline auto item_parent() noexcept { return m_parent; }
  [[nodiscard]] inline auto item_offset() noexcept { return m_item->dwOffset; }
  [[nodiscard]] inline auto item() noexcept { return m_item; }

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
  auto getTreeWidgetItem(VGMItem *vgm_item) { return m_items.at(vgm_item); };
  void updateStatusBar();

protected:
  void focusInEvent(QFocusEvent* event) override;
  void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
  void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void scrollContentsBy(int dx, int dy) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  int getSortedIndex(QTreeWidgetItem* parent, VGMTreeItem* item);
  void setItemText(VGMItem* item, VGMTreeItem* treeItem);
  void onShowDetailsChanged(bool showDetails);
  void updateItemTextRecursively(QTreeWidgetItem* item);

  bool showDetails = false;
  QTreeWidgetItem *parent_item_cached{};
  VGMItem *parent_cached{};
  std::unordered_map<VGMItem*, QTreeWidgetItem*> m_items{};
  std::unordered_map<QTreeWidgetItem*, VGMItem*> m_treeItemToVGMItem{};
};