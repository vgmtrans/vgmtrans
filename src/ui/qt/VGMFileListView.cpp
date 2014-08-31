//
// Created by Mike on 8/31/14.
//

#include "VGMFileListView.h"
#include "QtVGMRoot.h"
#include "VGMFile.h"

VGMFileListViewModel::VGMFileListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedVGMFile()), this, SLOT(addedVGMFiles()));
}

int VGMFileListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vVGMFile.size();
}

QVariant VGMFileListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*qtVGMRoot.vVGMFile[index.row()]->GetName());
    }
    return QVariant();
}

void VGMFileListViewModel::addedVGMFiles()
{
    emit dataChanged(index(0, 0), index(0, 0));
}