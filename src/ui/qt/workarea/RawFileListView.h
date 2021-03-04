//
// Created by Mike on 8/31/14.
//


#include <QAbstractListModel>
#include <QListView>
#include "../QtVGMRoot.h"

#ifndef __RawFileListView_H_
#define __RawFileListView_H_


class RawFileListViewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    RawFileListViewModel(QObject *parent = 0);

    int rowCount ( const QModelIndex & parent = QModelIndex() ) const;
    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

public slots:
    void changedRawFiles();
};



class RawFileListView : public QListView
{
public:
    RawFileListView(QWidget *parent = 0);

    void keyPressEvent(QKeyEvent* e);
};

#endif //__RawFileListView_H_
