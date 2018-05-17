/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QKeyEvent>
#include <QStandardItem>
#include "RawFileListView.h"

/*
* RawFileListViewModel
*/

RawFileListViewModel::RawFileListViewModel(QObject *parent)
    : QAbstractListModel(parent) {
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedRawFile()), this, SLOT(changedRawFiles()));
    QObject::connect(&qtVGMRoot, SIGNAL(UI_RemovedRawFile()), this, SLOT(changedRawFiles()));
}

int RawFileListViewModel::rowCount (const QModelIndex & parent) const {
    return static_cast<int>(qtVGMRoot.vRawFile.size());
}

QVariant RawFileListViewModel::data (const QModelIndex & index, int role ) const {
  if (role == Qt::DisplayRole) {
      const wchar_t *filename = qtVGMRoot.vRawFile[index.row()]->GetFileName();
      return QString::fromWCharArray(filename);
  } else if (role == Qt::DecorationRole) {
      return QIcon(":/images/file-32.png");
  }

  return QVariant();
}

void RawFileListViewModel::changedRawFiles() {
    emit dataChanged(index(0, 0), index(0, 0));
}

/*
 * RawFileListView
 */

RawFileListView::RawFileListView(QWidget *parent)
        : QListView(parent) {
    rawFileListViewModel = new RawFileListViewModel(this);
    this->setModel(rawFileListViewModel);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setSelectionRectVisible(true);
}

void RawFileListView::keyPressEvent(QKeyEvent * input) {
  // On Backspace or Delete keypress, remove all selected files
  switch(input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    {
      QModelIndexList list = this->selectionModel()->selectedIndexes();

      if(list.isEmpty())
        return;

      QList<RawFile*> filesToClose;
      foreach(const QModelIndex &index, list) {
        if(index.row() < qtVGMRoot.vRawFile.size())
          filesToClose.append(qtVGMRoot.vRawFile[index.row()]);
      }

      foreach(RawFile *file, filesToClose) {
        qtVGMRoot.CloseRawFile(file);
      }

      return;
    }
  }

}