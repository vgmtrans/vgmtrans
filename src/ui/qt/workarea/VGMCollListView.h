/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include <QListView>
#include <QKeyEvent>

class VGMColl;
class VGMCollListView;

class VGMCollListViewModel : public QAbstractListModel {
public:
  explicit VGMCollListViewModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
  bool resettingModel = false;
};

class VGMCollNameEditor : public QStyledItemDelegate {
protected:
  void setEditorData(QWidget *editor, const QModelIndex &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model,
                    const QModelIndex &index) const override;
};

class VGMCollListView : public QListView {
  Q_OBJECT
public:
  explicit VGMCollListView(QWidget *parent = nullptr);

signals:
  void nothingToPlay();

public slots:
  void handlePlaybackRequest();
  void handleStopRequest();

private:
  void collectionMenu(const QPoint &pos);
  void keyPressEvent(QKeyEvent *e) override;
};