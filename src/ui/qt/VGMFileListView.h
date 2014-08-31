//
// Created by Mike on 8/31/14.
//

#include <QAbstractListModel>

#ifndef __VGMFileListView_H_
#define __VGMFileListView_H_


class VGMFileListViewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    VGMFileListViewModel(QObject *parent = 0);

    int rowCount ( const QModelIndex & parent = QModelIndex() ) const;

    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const;

public slots:
    void addedVGMFiles();
};


#endif //__VGMFileListView_H_
