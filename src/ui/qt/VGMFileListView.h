/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QEvent>
#include <QListView>

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
  void doubleClickedSlot(QModelIndex);

signals:
  void AddMdiTab(QWidget *vgm_file, Qt::WindowFlags flags);

private:
  void keyPressEvent(QKeyEvent *input);
  void ItemMenu(const QPoint &pos);
};
