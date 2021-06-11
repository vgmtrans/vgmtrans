/**
* VGMTrans (c) - 2002-2021
* Licensed under the zlib license
* See the included LICENSE for more information
*/

#pragma once

#include <QAbstractListModel>
#include <QListView>
#include "MusicPlayer.h"

class VGMCollListViewModel : public QAbstractListModel {
  Q_OBJECT

public:
  explicit VGMCollListViewModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;

public slots:
  void changedVGMColls();
};

class VGMCollListView : public QListView {
public:
  explicit VGMCollListView(QWidget *parent = nullptr);
  void keyPressEvent(QKeyEvent *e) override;
};
