#pragma once

#include <QAbstractListModel>
#include <QListView>


class VGMCollListViewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    VGMCollListViewModel(QObject *parent = 0);

    int rowCount ( const QModelIndex & parent = QModelIndex() ) const;

    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const;

public slots:
    void changedVGMColls();
};



class VGMCollListView : public QListView
{
public:
  VGMCollListView(QWidget *parent = 0);

  void keyPressEvent(QKeyEvent* e);
  void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

 protected:
  void resizeEvent(QResizeEvent *event);

};
