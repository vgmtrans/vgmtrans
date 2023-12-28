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
#include "Helpers.h"
#include "QtVGMRoot.h"
#include "MdiArea.h"
#include "services/commands/GeneralCommands.h"

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
        return QString::fromStdString(*vgmfile->GetName());
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

  return {};
}

QVariant VGMFileListModel::headerData(int column, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical || role != Qt::DisplayRole) {
    return {};
  }

  switch (column) {
    case Property::Name: {
      return "Name";
    }

    case Property::Format: {
      return "Format";
    }

    default: {
      return {};
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
  setSelectionMode(QAbstractItemView::ExtendedSelection);
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
  header_hor->setSectionResizeMode(QHeaderView::Interactive);
  header_hor->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header_hor->setSectionResizeMode(1, QHeaderView::Fixed);
  header_hor->setStyleSheet("QHeaderView::section { padding-left: 6px; }");

  // set the "Format" section header size
  QFont headerFont = header_hor->font();
  QFontMetrics fm(headerFont);
  QVariant headerTextVariant = view_model->headerData(1, Qt::Horizontal, Qt::DisplayRole);
  auto headerText = headerTextVariant.isValid() ? headerTextVariant.toString() : "Format";
  int textWidth = fm.horizontalAdvance(headerText);
  setColumnWidth(1, textWidth + 45);

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(header_hor, &QHeaderView::sectionResized, this, &VGMFileListView::onHeaderSectionResized);
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemoveVGMFile, this, &VGMFileListView::removeVGMFile);
  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::itemMenu);
  connect(this, &QAbstractItemView::doubleClicked, this, &VGMFileListView::requestVGMFileView);
}

void VGMFileListView::onHeaderSectionResized(int index, int oldSize, int newSize) {
  // When a header section is resized, the other should full the rest of the header width
  int otherIndex = (index == 0) ? 1 : 0;
  int otherSize = width() - newSize;

  setColumnWidth(otherIndex, otherSize);
}

void VGMFileListView::itemMenu(const QPoint &pos) {

  auto selectedFiles = make_shared<vector<VGMFile*>>();

  if (!selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();

  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedFiles->push_back(qtVGMRoot.vVGMFile[index.row()]);
    }
  }
  auto menu = menuManager.CreateMenuForItems<VGMItem>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMFileListView::keyPressEvent(QKeyEvent *input) {
  switch (input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      if (!selectionModel()->hasSelection())
        return;

      QModelIndexList list = selectionModel()->selectedRows();

      vector<VGMFile*> selectedFiles;
      selectedFiles.reserve(list.size());
      for (const auto &index : list) {
        if (index.isValid()) {
          selectedFiles.push_back(qtVGMRoot.vVGMFile[index.row()]);
        }
      }

      pRoot->UI_BeginRemoveVGMFiles();
      for (auto vgmfile : selectedFiles) {
        vgmfile->OnClose();
      }
      pRoot->UI_EndRemoveVGMFiles();

      this->clearSelection();
      return;
    }

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QTableView::keyPressEvent(input);
  }
}

void VGMFileListView::scrollContentsBy(int dx, int dy) {
  // Call the base class implementation with dx set to 0 to disable horizontal scrolling
  // We disable horizontal scrolling so that we can hide final header column splitter
  QTableView::scrollContentsBy(0, dy);
}

void VGMFileListView::resizeEvent(QResizeEvent *event) {
  QAbstractItemView::resizeEvent(event);
  resizeColumns();
}

void VGMFileListView::resizeColumns() {
  // Ensure the columns take up the full width of the header. Only the "Name" column should expand/contract
  int formatColumnWidth = columnWidth(1);
  setColumnWidth(0, width() - formatColumnWidth);
}

void VGMFileListView::removeVGMFile(VGMFile *file) {
  MdiArea::the()->removeView(file);
  view_model->RemoveVGMFile();
}

void VGMFileListView::requestVGMFileView(QModelIndex index) {
  MdiArea::the()->newView(qtVGMRoot.vVGMFile[index.row()]);
}
