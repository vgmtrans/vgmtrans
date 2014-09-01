//
// Created by Mike on 8/31/14.
//

#include <QAbstractListModel>
#include <QEvent>
#include <QListView>

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
    void changedVGMFiles();
};



class VGMFileListView : public QListView
{
public:
    VGMFileListView(QWidget *parent = 0);

    void keyPressEvent(QKeyEvent* e);
};


#endif //__VGMFileListView_H_
