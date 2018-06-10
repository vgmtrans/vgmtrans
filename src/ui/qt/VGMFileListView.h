/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QEvent>
#include <QListView>
#include <unordered_map>

#include "VGMFileView.h"

class VGMFileListViewModel : public QAbstractListModel {
  Q_OBJECT

public:
  VGMFileListViewModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
};

class VGMFileListView : public QListView {
  Q_OBJECT

public:
  VGMFileListView(QWidget *parent = nullptr);

public slots:
  void RemoveVGMFileItem(VGMFile *file);
  void doubleClickedSlot(QModelIndex);

signals:
  void AddMdiTab(QWidget *vgm_file, Qt::WindowFlags flags);
  void RemoveMdiTab(VGMFileView *file_view);

private:
  void keyPressEvent(QKeyEvent *input);
  void ItemMenu(const QPoint &pos);

  std::unordered_map<VGMFile *, VGMFileView *> open_views;
};
