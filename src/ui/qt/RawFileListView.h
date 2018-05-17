/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QAbstractListModel>
#include <QListView>
#include "QtVGMRoot.h"

#ifndef RAWFILELISTVIEW_H
#define RAWFILELISTVIEW_H

class RawFileListViewModel : public QAbstractListModel {
    Q_OBJECT

public:
  RawFileListViewModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;

public slots:
    void changedRawFiles();

};

class RawFileListView : public QListView {
  Q_OBJECT

public:
  explicit RawFileListView(QWidget *parent = nullptr);

private:
  void keyPressEvent(QKeyEvent* input) override;

  RawFileListViewModel *rawFileListViewModel;

};

#endif // RAWFILELISTVIEW_H
