#include <QKeyEvent>
#include <QStandardItem>
#include "RawFileListView.h"
#include "main/RawFile.h"
#include "main/Core.h"
#include "ui/qt/QtUICallbacks.h"


// ********************
// RawFileListViewModel
// ********************

RawFileListViewModel::RawFileListViewModel(QObject *parent)
  : QAbstractListModel(parent)
{
  QObject::connect(&qtUICallbacks, SIGNAL(UI_AddedRawFile()), this, SLOT(changedRawFiles()));
  QObject::connect(&qtUICallbacks, SIGNAL(UI_RemovedRawFile()), this, SLOT(changedRawFiles()));
}

int RawFileListViewModel::rowCount ( const QModelIndex & parent) const
{
  return core.vRawFile.size();
}

QVariant RawFileListViewModel::data ( const QModelIndex & index, int role ) const
{
  if (role == Qt::DisplayRole) {
    const wchar_t *filename = core.vRawFile[index.row()]->GetFileName();
    return QString::fromWCharArray(filename);
  }
  else if (role == Qt::DecorationRole) {
    return QIcon(":/images/file-32.png");
  }

  return QVariant();
}

QVariant RawFileListViewModel::headerData(int section, Qt::Orientation orientation, int role) const {
  return "Test Header";
}

void RawFileListViewModel::changedRawFiles()
{
  emit dataChanged(index(0, 0), index(0, 0));
}


// ***************
// RawFileListView
// ***************

RawFileListView::RawFileListView(QWidget *parent)
  : QListView(parent)
{
  setAttribute(Qt::WA_MacShowFocusRect, 0);

  RawFileListViewModel *rawFileListViewModel = new RawFileListViewModel(this);
  this->setModel(rawFileListViewModel);
  this->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->setSelectionRectVisible(true);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void RawFileListView::keyPressEvent(QKeyEvent* e)
{
  // On Backspace or Delete keypress, remove all selected files
  if( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
  {
    QModelIndexList list = this->selectionModel()->selectedIndexes();

    if (list.isEmpty())
      return;

    QList<RawFile*> filesToClose;
    foreach(const QModelIndex &index, list) {
      if (index.row() < core.vRawFile.size())
        filesToClose.append(core.vRawFile[index.row()]);
    }


    foreach(RawFile *file, filesToClose) {
        printf("In Loop");
        core.CloseRawFile(file);
    }
  }
  else
      QListView::keyPressEvent(e);
}