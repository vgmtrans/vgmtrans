/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QListView>
#include "QtVGMRoot.h"

class RawFileListViewModel : public QAbstractListModel {
  Q_OBJECT

public:
  RawFileListViewModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
};

class RawFileListView : public QListView {
  Q_OBJECT

public:
  explicit RawFileListView(QWidget *parent = nullptr);

private:
  void keyPressEvent(QKeyEvent *input) override;
  void RawFilesMenu(const QPoint &pos);
  void DeleteRawFiles();

  RawFileListViewModel *rawFileListViewModel;
};
