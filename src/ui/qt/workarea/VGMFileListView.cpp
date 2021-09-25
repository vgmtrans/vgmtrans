/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QHeaderView>
#include <QMenu>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>

#include "VGMFileListView.h"
#include "VGMFileView.h"
#include "Helpers.h"
#include "QtVGMRoot.h"
#include "MdiArea.h"

/*
 *  VGMFileListModel
 */

VGMFileListModel::VGMFileListModel(QObject *parent) : QAbstractTableModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMFile, this, &VGMFileListModel::AddVGMFile);
}

QVariant VGMFileListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  VGMFile *vgmfile = qtVGMRoot.vVGMFile.at(static_cast<unsigned long>(index.row()));

  switch (index.column()) {
    case Property::Name: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*vgmfile->GetName());
      } else if (role == Qt::DecorationRole) {
        return iconForFileType(vgmfile->GetFileType());
      }
      break;
    }

    case Property::Format: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->GetFormatName());
      }
      break;
    }
  }

  return QVariant();
}

QVariant VGMFileListModel::headerData(int column, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical || role != Qt::DisplayRole) {
    return QVariant();
  }

  switch (column) {
    case Property::Name: {
      return "Name";
    }

    case Property::Format: {
      return "Format";
    }

    default: {
      return QVariant();
    }
  }
}

int VGMFileListModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(qtVGMRoot.vVGMFile.size());
}

int VGMFileListModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return 2;
}

void VGMFileListModel::AddVGMFile() {
  int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
  beginInsertRows(QModelIndex(), position, position);
  endInsertRows();
}

void VGMFileListModel::RemoveVGMFile() {
  int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
  if (position < 0) {
    return;
  }

  beginRemoveRows(QModelIndex(), position, position);
  endRemoveRows();
}

/*
 *  VGMFileListView
 */

VGMFileListView::VGMFileListView(QWidget *parent) : QTableView(parent) {
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setAlternatingRowColors(true);
  setShowGrid(false);
  setSortingEnabled(false);
  setContextMenuPolicy(Qt::CustomContextMenu);
  setWordWrap(false);

  view_model = new VGMFileListModel();
  setModel(view_model);

  verticalHeader()->hide();
  auto header_hor = horizontalHeader();
  header_hor->setSectionsMovable(true);
  header_hor->setHighlightSections(true);
  header_hor->setSectionResizeMode(QHeaderView::Stretch);

  connect(&qtVGMRoot, &QtVGMRoot::UI_RemoveVGMFile, this, &VGMFileListView::removeVGMFile);
  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::itemMenu);
  connect(this, &QAbstractItemView::doubleClicked, this, &VGMFileListView::requestVGMFileView);
}

void VGMFileListView::itemMenu(const QPoint &pos) {
  auto element = indexAt(pos);
  if (!element.isValid())
    return;

  VGMFile *file = qtVGMRoot.vVGMFile[element.row()];
  if (!file) {
    return;
  }

  /* This machinery is not great, no safe alternative until backend becomes type-erased */
  auto vgmfile_menu = new QMenu();
  vgmfile_menu->addAction("Remove", [file] { file->OnClose(); });
  vgmfile_menu->addAction("Save raw format", [file] { file->OnSaveAsRaw(); });
  vgmfile_menu->addSeparator();

  /* todo: implement free functions to export this stuff */
  switch (file->GetFileType()) {
    case FileType::FILETYPE_INSTRSET: {
      auto set = dynamic_cast<VGMInstrSet *>(file);
      vgmfile_menu->addAction("Save as DLS", [set] { set->OnSaveAsDLS(); });
      vgmfile_menu->addAction("Save as SF2", [set] { set->OnSaveAsSF2(); });
      break;
    }

    case FileType::FILETYPE_SEQ: {
      auto seq = dynamic_cast<VGMSeq *>(file);
      vgmfile_menu->addAction("Save as MIDI", [seq] { seq->OnSaveAsMidi(); });
      break;
    }

    case FileType::FILETYPE_SAMPCOLL: {
      auto samps = dynamic_cast<VGMSampColl *>(file);
      vgmfile_menu->addAction("Save as WAV", [samps] { samps->OnSaveAllAsWav(); });
      break;
    }

    default:
      break;
  }

  vgmfile_menu->exec(mapToGlobal(pos));
  vgmfile_menu->deleteLater();
}

void VGMFileListView::keyPressEvent(QKeyEvent *input) {
  switch (input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      if (!selectionModel()->hasSelection())
        return;

      QModelIndexList list = selectionModel()->selectedRows();
      for (auto &index : list) {
        auto file = qtVGMRoot.vVGMFile[index.row()];
        file->OnClose();
      }

      return;
    }

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QTableView::keyPressEvent(input);
  }
}

void VGMFileListView::removeVGMFile(VGMFile *file) {
  MdiArea::the()->removeView(file);
  view_model->RemoveVGMFile();
}

void VGMFileListView::requestVGMFileView(QModelIndex index) {
  MdiArea::the()->newView(qtVGMRoot.vVGMFile[index.row()]);
}
