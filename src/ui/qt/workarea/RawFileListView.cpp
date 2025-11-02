/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <ranges>

#include <QKeyEvent>
#include "RawFileListView.h"

#include "LogManager.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "QtVGMRoot.h"
#include "services/NotificationCenter.h"
#include "services/MenuManager.h"
#include "VGMExport.h"

static const QIcon& fileIcon() {
  static QIcon fileIcon(":/icons/file.svg");
  return fileIcon;
}

/*
 * RawFileListViewModel
 */

RawFileListViewModel::RawFileListViewModel(QObject *parent) : QAbstractTableModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedRawFile, this, &RawFileListViewModel::addRawFile);
  connect(&qtVGMRoot, &QtVGMRoot::UI_removedRawFile, this, &RawFileListViewModel::removeRawFile);
}

int RawFileListViewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;

  return static_cast<int>(qtVGMRoot.rawFiles().size());
}

int RawFileListViewModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;

  return 2;
}

void RawFileListViewModel::addRawFile() {
  int position = static_cast<int>(qtVGMRoot.rawFiles().size()) - 1;
  if (position >= 0) {
    beginInsertRows(QModelIndex(), position, position);
    endInsertRows();
  }
}

void RawFileListViewModel::removeRawFile() {
  int position = static_cast<int>(qtVGMRoot.rawFiles().size()) - 1;
  if (position >= 0) {
    beginRemoveRows(QModelIndex(), position, position);
    endRemoveRows();
  } else {
    // hack to refresh the view when deleting the last column
    dataChanged(index(0, 0), index(0, 0));
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
        return QString::fromStdString(qtVGMRoot.rawFiles()[index.row()]->name());
      } else if (role == Qt::DecorationRole) {
        return fileIcon();
      }
      break;
    }

    case Property::ContainedFiles: {
      if (role == Qt::DisplayRole) {
        return QString::number(qtVGMRoot.rawFiles()[index.row()]->containedVGMFiles().size());
      }
      break;
    }
    default:
      L_WARN("requested data for unexpected column index: {}", index.column());
      break;
  }

  return QVariant();
}

/*
 * RawFileListView
 */

RawFileListView::RawFileListView(QWidget *parent) : TableView(parent) {
  rawFileListViewModel = new RawFileListViewModel(this);
  RawFileListView::setModel(rawFileListViewModel);
  setIconSize(QSize(16, 16));

  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  setContextMenuPolicy(Qt::CustomContextMenu);

  connect(this, &QAbstractItemView::customContextMenuRequested, this,
          &RawFileListView::rawFilesMenu);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &RawFileListView::onVGMFileSelected);
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &RawFileListView::onSelectionChanged);

  updateContextualMenus();
}

/*
 * This is different from the other context menus,
 * since the only possible action on a RawFile is removing it
 */
void RawFileListView::rawFilesMenu(const QPoint &pos) const {
  if (!selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();

  auto selectedFiles = std::make_shared<std::vector<RawFile*>>();
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedFiles->push_back(qtVGMRoot.rawFiles()[index.row()]);
    }
  }
  auto menu = MenuManager::the()->createMenuForItems<RawFile>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void RawFileListView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void RawFileListView::updateContextualMenus() const {
  if (!selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForRawFiles({});
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();
  QList<RawFile*> files;
  files.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      files.append(qtVGMRoot.rawFiles()[index.row()]);
    }
  }

  NotificationCenter::the()->updateContextualMenusForRawFiles(files);
}

void RawFileListView::keyPressEvent(QKeyEvent *input) {
  // On Backspace or Delete keypress, remove all selected files
  switch (input->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      deleteRawFiles();
      break;
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
  clearSelection();
  for (auto & idx : std::ranges::reverse_view(list)) {
    const auto rawfile = qtVGMRoot.rawFiles()[idx.row()];
    qtVGMRoot.closeRawFile(rawfile);
  }
}

void RawFileListView::onVGMFileSelected(const VGMFile* vgmfile, const QWidget* caller) {
  if (caller == this)
    return;

  if (vgmfile == nullptr) {
    this->clearSelection();
    return;
  }

  auto it = std::ranges::find(qtVGMRoot.rawFiles(), vgmfile->rawFile());
  if (it == qtVGMRoot.rawFiles().end())
    return;
  int row = static_cast<int>(std::distance(qtVGMRoot.rawFiles().begin(), it));

  // Select the row corresponding to the file
  QModelIndex firstIndex = model()->index(row, 0); // First column of the row
  QModelIndex lastIndex = model()->index(row, model()->columnCount() - 1); // Last column of the row

  QItemSelection selection(firstIndex, lastIndex);
  selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

  scrollTo(firstIndex, QAbstractItemView::EnsureVisible);
}

// Update the status bar on focus
void RawFileListView::focusInEvent(QFocusEvent *event) {
  TableView::focusInEvent(event);
  updateStatusBar();
}

void RawFileListView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
  TableView::currentChanged(current, previous);

  if (this->hasFocus())
    updateStatusBar();
}

// Update the status bar for the current selection
void RawFileListView::updateStatusBar() const {
  if (!currentIndex().isValid() || currentIndex().row() >= rawFileListViewModel->rowCount()) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }
  RawFile* file = qtVGMRoot.rawFiles()[currentIndex().row()];
  QString name = QString::fromStdString(file->name());
  NotificationCenter::the()->updateStatus(name, "", &fileIcon(), -1, static_cast<uint32_t>(file->size()));
}
