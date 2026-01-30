/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractTableModel>
#include <QKeyEvent>
#include <vector>
#include "TableView.h"

#include "VGMFile.h"

class QItemSelection;
class RawFile;
class VGMFileListModel : public QAbstractTableModel {
public:
  enum Property : uint8_t { Name = 0, Format = 1, Type = 2 };

  explicit VGMFileListModel(QObject *parent = nullptr);

  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

  [[nodiscard]] VGMFile *fileFromIndex(const QModelIndex &index) const;
  [[nodiscard]] QModelIndex indexFromFile(const VGMFile *file) const;
  [[nodiscard]] bool isHeaderRow(const QModelIndex &index) const;

private:
  struct RowEntry {
    bool isHeader = false;
    RawFile *raw = nullptr;
    VGMFile *file = nullptr;
  };

  void rebuildRows();
  int sortColumn = -1;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
  std::vector<RowEntry> rows;
};

class VGMFileListView final : public TableView {
  Q_OBJECT

public:
  explicit VGMFileListView(QWidget *parent = nullptr);

public slots:
  void requestVGMFileView(const QModelIndex& index);
  void onVGMFileSelected(VGMFile *file, const QWidget* caller);

private:
  void updateStatusBar() const;
  void focusInEvent(QFocusEvent *event) override;
  void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
  void keyPressEvent(QKeyEvent *input) override;
  void itemMenu(const QPoint &pos);
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void updateContextualMenus() const;

  VGMFileListModel *view_model;
};
