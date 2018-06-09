/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QKeyEvent>
#include <QMenu>
#include "VGMFileListView.h"
#include "QtVGMRoot.h"
#include "VGMFile.h"
#include "MdiArea.h"
#include "Helpers.h"

// ********************
// VGMFileListViewModel
// ********************

VGMFileListViewModel::VGMFileListViewModel(QObject *parent) : QAbstractListModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMFile,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedVGMFile,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
}

int VGMFileListViewModel::rowCount(const QModelIndex &parent) const {
  return qtVGMRoot.vVGMFile.size();
}

QVariant VGMFileListViewModel::data(const QModelIndex &index, int role) const {
  if (role == Qt::DisplayRole) {
    return QString::fromStdWString(*qtVGMRoot.vVGMFile[index.row()]->GetName());
  } else if (role == Qt::DecorationRole) {
    FileType filetype = qtVGMRoot.vVGMFile[index.row()]->GetFileType();
    return iconForFileType(filetype);
  }
  return QVariant();
}

// ***************
// VGMFileListView
// ***************

VGMFileListView::VGMFileListView(QWidget *parent) : QListView(parent) {
  VGMFileListViewModel *vgmFileListViewModel = new VGMFileListViewModel(this);
  setModel(vgmFileListViewModel);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setContextMenuPolicy(Qt::CustomContextMenu);
  setIconSize(QSize(16, 16));

  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::ItemMenu);
  connect(this, &QAbstractItemView::doubleClicked, this, &VGMFileListView::doubleClickedSlot);
}

/*
 * Definitely not the prettiest way to do it, but
 * it's all we can do with the current backend functions.
 * Identical implementation for VGMCollListView
 */
void VGMFileListView::ItemMenu(const QPoint &pos) {
  auto element = indexAt(pos);
  if (!element.isValid())
    return;

  VGMFile *pointed_vgmfile = qtVGMRoot.vVGMFile[element.row()];
  if (pointed_vgmfile == nullptr) {
    return;
  }

  QMenu *vgmfile_menu = new QMenu();
  std::vector<const wchar_t *> *menu_item_names = pointed_vgmfile->GetMenuItemNames();
  for (auto &menu_item : *menu_item_names) {
    vgmfile_menu->addAction(QString::fromStdWString(menu_item));
  }

  QAction *performed_action = vgmfile_menu->exec(mapToGlobal(pos));
  int action_index = 0;
  for (auto &action : vgmfile_menu->actions()) {
    if (performed_action == action) {
      pointed_vgmfile->CallMenuItem(pointed_vgmfile, action_index);
      break;
    }
    action_index++;
  }

  vgmfile_menu->deleteLater();
}

void VGMFileListView::keyPressEvent(QKeyEvent *input) {
  switch (input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      QModelIndexList list = selectionModel()->selectedIndexes();

      if (list.isEmpty())
        return;

      QList<VGMFile *> filesToClose;
      foreach (const QModelIndex &index, list) {
        if (index.row() < qtVGMRoot.vVGMFile.size())
          filesToClose.append(qtVGMRoot.vVGMFile[index.row()]);
      }

      foreach (VGMFile *file, filesToClose) { qtVGMRoot.RemoveVGMFile(file); }

      return;
    }

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QListView::keyPressEvent(input);
  }
}

void VGMFileListView::doubleClickedSlot(QModelIndex index) {
  VGMFile *vgmFile = qtVGMRoot.vVGMFile[index.row()];
  VGMFileView *vgmFileView = new VGMFileView(vgmFile);
  QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());

  vgmFileView->setWindowTitle(vgmFileName);
  vgmFileView->setWindowIcon(iconForFileType(vgmFile->GetFileType()));
  vgmFileView->setAttribute(Qt::WA_DeleteOnClose);

  AddMdiTab(vgmFileView, Qt::SubWindow);

  vgmFileView->show();
}