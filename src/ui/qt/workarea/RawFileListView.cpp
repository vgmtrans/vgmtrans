/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QKeyEvent>
#include <QMenu>
#include <QHeaderView>
#include "RawFileListView.h"
#include "RawFile.h"
#include "QtVGMRoot.h"

/*
 * RawFileListViewModel
 */

RawFileListViewModel::RawFileListViewModel(QObject *parent) : QAbstractTableModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedRawFile, this, &RawFileListViewModel::AddRawFile);
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedRawFile, this, &RawFileListViewModel::RemoveRawFile);
}

int RawFileListViewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;

  return static_cast<int>(qtVGMRoot.vRawFile.size());
}

int RawFileListViewModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;

  return 2;
}

void RawFileListViewModel::AddRawFile() {
  int position = static_cast<int>(qtVGMRoot.vRawFile.size()) - 1;
  if (position >= 0) {
    beginInsertRows(QModelIndex(), position, position);
    endInsertRows();
  }
}

void RawFileListViewModel::RemoveRawFile() {
  int position = static_cast<int>(qtVGMRoot.vRawFile.size());
  if (position >= 0) {
    dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
  }
}

QVariant RawFileListViewModel::headerData(int column, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical || role != Qt::DisplayRole)
    return QVariant();

  switch (column) {
    case Property::Name: {
      return "Name";
    }

    case Property::ContainedFiles: {
      return "Contained files";
    }

    default: {
      return QVariant();
    }
  }
}

QVariant RawFileListViewModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  switch (index.column()) {
    case Property::Name: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(qtVGMRoot.vRawFile[index.row()]->GetFileName());
      } else if (role == Qt::DecorationRole) {
        static QIcon fileicon(":/images/file.svg");
        return fileicon;
      }
      break;
    }

    case Property::ContainedFiles: {
      if (role == Qt::DisplayRole) {
        return QString::number(qtVGMRoot.vRawFile[index.row()]->containedVGMFiles.size());
      }
      break;
    }
  }

  return QVariant();
}

/*
 * RawFileListView
 */

RawFileListView::RawFileListView(QWidget *parent) : QTableView(parent) {
  rawFileListViewModel = new RawFileListViewModel(this);
  setModel(rawFileListViewModel);
  setAlternatingRowColors(true);
  setShowGrid(false);
  setWordWrap(false);
  setIconSize(QSize(16, 16));

  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  verticalHeader()->hide();
  auto header_hor = horizontalHeader();
  header_hor->setSectionsMovable(true);
  header_hor->setHighlightSections(true);
  header_hor->setSectionResizeMode(QHeaderView::Stretch);

  setContextMenuPolicy(Qt::CustomContextMenu);
  rawfile_context_menu = new QMenu();
  QAction *rawfile_remove = rawfile_context_menu->addAction("Remove");
  connect(rawfile_remove, &QAction::triggered, this, &RawFileListView::deleteRawFiles);
  rawfile_context_menu->addAction("Save raw unpacked image(s)", [sm = selectionModel()]() {
    if (!sm->hasSelection())
      return;

    QModelIndexList list = sm->selectedRows();
    for (auto &index : list) {
      auto rawfile = qtVGMRoot.vRawFile[index.row()];
      std::string filepath = pRoot->UI_GetSaveFilePath("");
      if (!filepath.empty()) {
        /* todo: free function in VGMExport */
        rawfile->OnSaveAsRaw();
      }
    }
  });

  connect(this, &QAbstractItemView::customContextMenuRequested, this,
          &RawFileListView::rawFilesMenu);
}

/*
 * This is different from the other context menus,
 * since the only possible action on a RawFile is removing it
 */
void RawFileListView::rawFilesMenu(const QPoint &pos) {
  if (!indexAt(pos).isValid())
    return;

  rawfile_context_menu->exec(mapToGlobal(pos));
}

void RawFileListView::keyPressEvent(QKeyEvent *input) {
  // On Backspace or Delete keypress, remove all selected files
  switch (input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      deleteRawFiles();
    }

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QTableView::keyPressEvent(input);
  }
}

void RawFileListView::deleteRawFiles() {
  if (!selectionModel()->hasSelection())
    return;

  QModelIndexList list = selectionModel()->selectedRows();
  std::vector<RawFile *> to_close;
  to_close.reserve(list.size());
  for (auto &index : list) {
    to_close.emplace_back(qtVGMRoot.vRawFile[index.row()]);
  }

  std::for_each(std::begin(to_close), std::end(to_close),
                [](RawFile *file) { qtVGMRoot.CloseRawFile(file); });
}