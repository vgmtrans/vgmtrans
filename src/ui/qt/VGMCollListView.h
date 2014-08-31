//
// Created by Mike on 8/31/14.
//

#include <QAbstractListModel>

#ifndef __VGMCollListViewModel_H_
#define __VGMCollListViewModel_H_


class VGMCollListViewModel : public QAbstractListModel
{
    Q_OBJECT

public:
    VGMCollListViewModel(QObject *parent = 0);

    int rowCount ( const QModelIndex & parent = QModelIndex() ) const;

    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const;

public slots:
    void addedVGMColl();
};

#endif //__VGMCollListViewModel_H_
