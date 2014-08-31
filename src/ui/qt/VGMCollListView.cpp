//
// Created by Mike on 8/31/14.
//

#include "VGMCollListView.h"
#include "QtVGMRoot.h"
#include "VGMColl.h"

VGMCollListViewModel::VGMCollListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedVGMColl()), this, SLOT(addedVGMColl()));
}

int VGMCollListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vVGMColl.size();
}

QVariant VGMCollListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*qtVGMRoot.vVGMColl[index.row()]->GetName());
    }
    return QVariant();
}

void VGMCollListViewModel::addedVGMColl()
{
    emit dataChanged(index(0, 0), index(0, 0));
}