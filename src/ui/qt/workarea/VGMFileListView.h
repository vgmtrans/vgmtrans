/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractTableModel>
#include <QWidget>
#include <vector>

#include "VGMFile.h"

class QComboBox;
class QItemSelection;
class QPushButton;
class TableView;
class VGMFileListModel : public QAbstractTableModel {
public:
  enum Property : uint8_t { Name = 0, Format = 1 };
  enum class SortKey { Added, Type, Format, Name };

  explicit VGMFileListModel(QObject *parent = nullptr);

  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  [[nodiscard]] VGMFile *fileFromIndex(const QModelIndex &index) const;
  [[nodiscard]] QModelIndex indexFromFile(const VGMFile *file) const;
  void setSort(SortKey key, Qt::SortOrder order);

private:
  void rebuildRows();
  SortKey sortKey = SortKey::Added;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
  std::vector<VGMFile *> rows;
};

class VGMFileListView final : public QWidget {
  Q_OBJECT

public:
  explicit VGMFileListView(QWidget *parent = nullptr);

public slots:
  void requestVGMFileView(const QModelIndex& index);
  void onVGMFileSelected(VGMFile *file, const QWidget* caller);

private:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void updateStatusBar() const;
  void handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
  void itemMenu(const QPoint &pos);
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void updateContextualMenus() const;
  void applySort();
  void updateSortButtonIcon();

  TableView *m_table;
  QComboBox *m_sortCombo;
  QPushButton *m_sortOrderButton;
  VGMFileListModel *view_model;
  Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
