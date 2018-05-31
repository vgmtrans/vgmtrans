#pragma once

#include <QAbstractListModel>
#include <QListView>

class VGMColl;
class VGMFile;

class VGMCollViewModel : public QAbstractListModel {
  Q_OBJECT

 public:
  VGMCollViewModel(QItemSelectionModel* collListSelModel, QObject *parent = 0);

  int rowCount ( const QModelIndex & parent = QModelIndex() ) const;

  QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const;

  VGMFile* fileFromIndex(QModelIndex index) const;

 public slots:
  void handleNewCollSelected(QModelIndex modelIndex);

 private:
  VGMColl* coll;
};



class VGMCollView : public QListView
{
 Q_OBJECT

 public:
  VGMCollView(QItemSelectionModel* collListSelModel, QWidget *parent = 0);
//  void keyPressEvent(QKeyEvent* e);

 public slots:
  void doubleClickedSlot(QModelIndex);
};
