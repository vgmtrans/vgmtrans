/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QKeyEvent>
#include <QMenu>
#include "RawFileListView.h"
#include "RawFile.h"

/*
* RawFileListViewModel
*/

RawFileListViewModel::RawFileListViewModel(QObject *parent)
  : QAbstractListModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedRawFile, [=]() { dataChanged(index(0, 0), index(0, 0)); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedRawFile, [=]() { dataChanged(index(0, 0), index(0, 0)); });
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

/*
 * RawFileListView
 */

RawFileListView::RawFileListView(QWidget *parent)
        : QListView(parent) {
    rawFileListViewModel = new RawFileListViewModel(this);
    setModel(rawFileListViewModel);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIconSize(QSize(16, 16));

    connect(this, &QAbstractItemView::customContextMenuRequested, this, &RawFileListView::RawFilesMenu);
}

/*
* This is different from the other context menus,
* since the only possible action on a RawFile is removing it
*/
void RawFileListView::RawFilesMenu(const QPoint &pos) {
  if(!indexAt(pos).isValid())
    return;

  QMenu *rawfiles_menu = new QMenu();
  QAction *rawfile_remove = rawfiles_menu->addAction("Remove");
  connect(rawfile_remove, &QAction::triggered, this, &RawFileListView::DeleteRawFiles);
  rawfiles_menu->exec(mapToGlobal(pos));
  
  rawfiles_menu->deleteLater();
}

void RawFileListView::keyPressEvent(QKeyEvent * input) {
  // On Backspace or Delete keypress, remove all selected files
  switch(input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    {
      DeleteRawFiles();
    }
  }

}

void RawFileListView::DeleteRawFiles(){
  QModelIndexList list = selectionModel()->selectedIndexes();

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