//
// Created by Mike on 8/31/14.
//

#include "RawFileListView.h"
#include "QtVGMRoot.h"

RawFileListViewModel::RawFileListViewModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedRawFile()), this, SLOT(addedRawFiles()));
}

int RawFileListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vRawFile.size();
}

QVariant RawFileListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        const wchar_t *filename = qtVGMRoot.vRawFile[index.row()]->GetFileName();
        return QString::fromWCharArray(filename);
    }
    return QVariant();
}

void RawFileListViewModel::addedRawFiles()
{
    emit dataChanged(index(0, 0), index(0, 0));
}


// RawFileListView


RawFileListView::RawFileListView(QWidget *parent)
        : QListView(parent)
{
}

void RawFileListView::keyPressEvent(QKeyEvent* e)
{
    printf("blah");
}