#include <QDebug>
#include "VGMFileItemModel.h"
#include "VGMFile.h"
#include <QIcon>

VGMFileItemModel::VGMFileItemModel(VGMFile *file) : QAbstractItemModel(), vgmfile(file) {
}

QModelIndex VGMFileItemModel::index(int row, int column, const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent))
    return QModelIndex();

  if (row < this->vgmfile->containers.size()) {
    return createIndex(row, column, this->vgmfile->containers[row]);
  }
  return createIndex(row, column, this->vgmfile->localitems[row - vgmfile->containers.size()]);
  //    QModelIndex()
  //    this->vgmfile->localitems[row]
}
QModelIndex VGMFileItemModel::parent(const QModelIndex &index) const {
  //    return createIndex(0, 0, vgmfile);
  return QModelIndex();

  //    if(!index.isValid())
  //        return QModelIndex();
  //
  //    VGMItem* childItem = static_cast<LayersModelItem*>(index.internalPointer());
  //    VGMItem* parentItem = childItem->GetItemFromOffset()
}
int VGMFileItemModel::rowCount(const QModelIndex &parent) const {
  if (parent.column() > 0)
    return 0;
  if (!parent.isValid()) {
    //        qDebug() << "localitems.size() : " << vgmfile->localitems.size();
    //        qDebug() << "containers.size() : " << vgmfile->containers.size();
    for (int i = 0; i < vgmfile->containers.size(); i++) {
      //            qDebug() << "i: " << i;
      //            qDebug() << "containers->size() : " << vgmfile->containers[i]->size();
      //            qDebug() << "containers->localitems->size() : " << vgmfile->containers[i]
    }

    return vgmfile->containers.size() + vgmfile->localitems.size();
  }
  VGMContainerItem *item = static_cast<VGMContainerItem *>(parent.internalPointer());
  return item->containers.size() + item->localitems.size();
  //    return static_cast<VGMContainerItem*>(parent.internalPointer())->localitems.size();
}

int VGMFileItemModel::columnCount(const QModelIndex &parent) const {
  return 1;
}

QVariant VGMFileItemModel::data(const QModelIndex &index, int role) const {
  return QVariant();
}
