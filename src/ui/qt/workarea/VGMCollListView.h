/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QKeyEvent>
#include <QListView>

class VGMFile;
class QItemSelection;

class VGMCollListViewModel : public QAbstractListModel {
public:
  explicit VGMCollListViewModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex &index, const QVariant &value, int role) override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
  size_t collsBeforeLoad;
  bool isLoadingRawFile = false;
};

class VGMCollListView : public QListView {
  Q_OBJECT
public:
  explicit VGMCollListView(QWidget *parent = nullptr);

signals:
  void nothingToPlay();

public slots:
  void handlePlaybackRequest();
  static void handleStopRequest();

private:
  void beginRenameCurrentSelection();
  void collectionMenu(const QPoint &pos);
  void keyPressEvent(QKeyEvent *e) override;
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void onVGMFileSelected(VGMFile* file, const QWidget* caller);
  void updateSelectedCollection() const;
  void updateContextualMenus() const;
};
