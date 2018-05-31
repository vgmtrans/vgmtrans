#include <src/main/VGMColl.h>
#include "VGMCollView.h"
#include "VGMFileView.h"
#include "MdiArea.h"
#include "main/Core.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"
#include "Helpers.h"


// ****************
// VGMCollViewModel
// ****************

VGMCollViewModel::VGMCollViewModel(QItemSelectionModel* collListSelModel, QObject *parent)
    : QAbstractListModel(parent),
      coll(NULL)
{
  QObject::connect(collListSelModel, SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(handleNewCollSelected(QModelIndex)));
}

int VGMCollViewModel::rowCount ( const QModelIndex & parent) const
{
  if (coll == NULL) {
    return 0;
  }
  return coll->instrsets.size() + coll->sampcolls.size() + coll->miscfiles.size() + 1;
}

QVariant VGMCollViewModel::data (const QModelIndex& index, int role) const
{
  VGMFile* file = fileFromIndex(index);

  if (role == Qt::DisplayRole) {
    return QString::fromStdWString(*file->GetName());
  }
  else if (role == Qt::DecorationRole) {
    FileType filetype = file->GetFileType();
    return iconForFileType(filetype);
  }
  return QVariant();
}

void VGMCollViewModel::handleNewCollSelected(QModelIndex modelIndex)
{
  if (!modelIndex.isValid()) {
    coll = NULL;
  }
  coll = core.vVGMColl[modelIndex.row()];

  emit dataChanged(index(0, 0), index(0, 0));
}

VGMFile* VGMCollViewModel::fileFromIndex(QModelIndex index) const {
  VGMFile* file;
  auto row = index.row();
  auto numInstrSets = coll->instrsets.size();
  auto numSampColls = coll->sampcolls.size();
  auto numMiscFiles = coll->miscfiles.size();
  if (row < numMiscFiles) {
    file = coll->miscfiles[row];
  }
  else {
    row -= numMiscFiles;
    if (row < numSampColls) {
      file = coll->instrsets[row];
    }
    else {
      row -= numInstrSets;
      if (row < numSampColls) {
        file = coll->sampcolls[row];
      }
      else {
        file = coll->seq;
      }
    }
  }
  return file;
}

// ***********
// VGMCollView
// ***********

VGMCollView::VGMCollView(QItemSelectionModel* collListSelModel, QWidget *parent)
  : QListView(parent)
{
  setAttribute(Qt::WA_MacShowFocusRect, 0);

  VGMCollViewModel *vgmCollViewModel = new VGMCollViewModel(collListSelModel, this);
  this->setModel(vgmCollViewModel);
  this->setSelectionMode(QAbstractItemView::ExtendedSelection);
  this->setSelectionRectVisible(true);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(doubleClickedSlot(QModelIndex)));
}

void VGMCollView::doubleClickedSlot(QModelIndex index)
{
  VGMFile *vgmFile = ((VGMCollViewModel*)model())->fileFromIndex(index);
  VGMFileView *vgmFileView = new VGMFileView(vgmFile);
  QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());
  vgmFileView->setWindowTitle(vgmFileName);
  vgmFileView->setWindowIcon(iconForFileType(vgmFile->GetFileType()));

  MdiArea::getInstance()->addSubWindow(vgmFileView);

  vgmFileView->show();
}