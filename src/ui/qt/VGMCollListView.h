/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QAbstractListModel>
#include <QListView>

#ifndef __VGMCollListViewModel_H_
#define __VGMCollListViewModel_H_


class VGMCollListViewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    VGMCollListViewModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;

};

class VGMCollListView : public QListView
{
public:
    VGMCollListView(QWidget *parent = nullptr);

private:
  void CollMenu(const QPoint &pos);
};

#endif //__VGMCollListViewModel_H_
