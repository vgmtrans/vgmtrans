#include <QKeyEvent>
#include <QDebug>
#include <QScrollBar>
#include "VGMFileListView.h"
#include "QtUICallbacks.h"
#include "VGMFile.h"
#include "VGMSeq.h"
#include "VGMSampColl.h"
#include "MdiArea.h"
#include "VGMFileView.h"
#include "Helpers.h"
#include "main/Core.h"

// ********************
// VGMFileListViewModel
// ********************

VGMFileListViewModel::VGMFileListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
  connect(&qtUICallbacks, SIGNAL(UI_AddedVGMFile()), this, SLOT(changedVGMFiles()));
  connect(&qtUICallbacks, SIGNAL(UI_RemovedVGMFile()), this, SLOT(changedVGMFiles()));
}

int VGMFileListViewModel::rowCount ( const QModelIndex & parent) const
{
  return core.vVGMFile.size();
}

QVariant VGMFileListViewModel::data ( const QModelIndex & index, int role ) const
{
  if (role == Qt::DisplayRole) {
    return QString::fromStdWString(*core.vVGMFile[index.row()]->GetName());
  }
  else if (role == Qt::DecorationRole) {
    FileType filetype = core.vVGMFile[index.row()]->GetFileType();
    return iconForFileType(filetype);
  }
  return QVariant();
}

void VGMFileListViewModel::changedVGMFiles()
{
  emit dataChanged(index(0, 0), index(0, 0));
}


// ***************
// VGMFileListView
// ***************

VGMFileListView::VGMFileListView(QWidget *parent)
        : QListView(parent)
{
  setAttribute(Qt::WA_MacShowFocusRect, 0);

  VGMFileListViewModel *vgmFileListViewModel = new VGMFileListViewModel(this);
  this->setModel(vgmFileListViewModel);
  this->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->setSelectionRectVisible(true);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    this->horizontalScrollBar()->setEnabled(false);

//    connect(this, SIGNAL(clicked(QModelIndex)),this,SLOT(myItemSelected(QModelIndex)));
  connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(doubleClickedSlot(QModelIndex)));
}

void VGMFileListView::keyPressEvent(QKeyEvent* e)
{
  QScrollBar* vertScrollBar = this->verticalScrollBar();
  QWidget* parentWidget = vertScrollBar->parentWidget();

// set black background
//  vertScrollBar->setStyleSheet("QScrollBar { background-color: rgba(255, 255, 255, 10); }");
//  parentWidget->setStyleSheet("QWidget { background-color: transparent; }");


  qDebug() << parentWidget;
  qDebug() << parentWidget->parentWidget();

  // On Backspace or Delete keypress, remove all selected files
  if( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
  {
    QModelIndexList list = this->selectionModel()->selectedIndexes();

    if (list.isEmpty())
      return;

    QList<VGMFile*> filesToClose;
    foreach(const QModelIndex &index, list) {
      if (index.row() < core.vVGMFile.size())
        filesToClose.append(core.vVGMFile[index.row()]);
    }

    foreach(VGMFile *file, filesToClose) {
      core.RemoveVGMFile(file);
    }
  }
  else if (e->key() == Qt::Key_S) {
    QModelIndexList list = this->selectionModel()->selectedIndexes();
    if (list.size() == 0 || list[0].row() >= core.vVGMFile.size())
      return;

    VGMFile* vgmfile = core.vVGMFile[list[0].row()];
    switch (vgmfile->GetFileType()) {
      case FILETYPE_SEQ: {
        VGMSeq *seq = (VGMSeq*) vgmfile;
        MidiFile *midi = seq->ConvertToMidi();
        wstring filename = *seq->GetName() + L".mid";
        midi->SaveMidiFile(filename);
        break;
      }
      case FILETYPE_SAMPCOLL: {
        VGMSampColl *sampColl = (VGMSampColl*) vgmfile;
        sampColl->OnSaveAllAsWav();
        break;
      }
    }
  }
  else
    QListView::keyPressEvent(e);
}

void VGMFileListView::doubleClickedSlot(QModelIndex index)
{
  VGMFile *vgmFile = core.vVGMFile[index.row()];
  VGMFileView *vgmFileView = new VGMFileView(vgmFile);
  QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());
  vgmFileView->setWindowTitle(vgmFileName);
  vgmFileView->setWindowIcon(iconForFileType(vgmFile->GetFileType()));

  MdiArea::getInstance()->addSubWindow(vgmFileView);

  vgmFileView->show();
}