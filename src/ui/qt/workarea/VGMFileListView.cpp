/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <algorithm>

#include <QFont>
#include <QHeaderView>

#include "VGMFileListView.h"
#include "Helpers.h"
#include "QtVGMRoot.h"
#include "MdiArea.h"
#include "Colors.h"
#include "TintableSvgIconEngine.h"
#include "services/NotificationCenter.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMSeq.h"
#include "VGMMiscFile.h"
#include "VGMSampColl.h"
#include "services/MenuManager.h"
#include "RawFile.h"

namespace {
const QIcon &sequenceInCollectionIcon() {
  static QIcon icon(new TintableSvgIconEngine(":/icons/sequence.svg", EventColors::CLR_GREEN));
  return icon;
}

QString typeLabelForFile(const VGMFile *file) {
  if (dynamic_cast<const VGMSeq *>(file)) {
    return "Seq";
  }
  if (dynamic_cast<const VGMInstrSet *>(file)) {
    return "InstrSet";
  }
  if (dynamic_cast<const VGMSampColl *>(file)) {
    return "SampColl";
  }
  return "Misc";
}

QString sortKeyForColumn(VGMFile *file, int column) {
  if (!file) {
    return {};
  }
  switch (column) {
    case VGMFileListModel::Property::Name:
      return QString::fromStdString(file->name()).toLower();
    case VGMFileListModel::Property::Format:
      return QString::fromStdString(file->formatName()).toLower();
    case VGMFileListModel::Property::Type:
      return typeLabelForFile(file).toLower();
    default:
      return {};
  }
}
}

/*
 *  VGMFileListModel
 */

VGMFileListModel::VGMFileListModel(QObject *parent) : QAbstractTableModel(parent) {
  rebuildRows();

  auto startResettingModel = [this]() { beginResetModel(); };
  auto endResettingModel = [this]() {
    rebuildRows();
    endResetModel();
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
  };

  connect(&qtVGMRoot, &QtVGMRoot::UI_beginLoadRawFile, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endLoadRawFile, endResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_beginRemoveVGMFiles, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveVGMFiles, endResettingModel);
  auto refreshIcons = [this]() {
    const int rows = rowCount();
    if (rows <= 0) {
      return;
    }
    emit dataChanged(index(0, 0), index(rows - 1, 0), {Qt::DecorationRole});
  };
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedVGMColl, refreshIcons);
  connect(&qtVGMRoot, &QtVGMRoot::UI_removedVGMColl, refreshIcons);
}

QVariant VGMFileListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  const auto &row = rows[static_cast<size_t>(index.row())];
  if (row.isHeader) {
    if (role == Qt::DisplayRole && index.column() == Property::Name && row.raw) {
      return QString::fromStdString(row.raw->name());
    }
    if (role == Qt::FontRole) {
      QFont font;
      font.setBold(true);
      return font;
    }
    return {};
  }

  VGMFile *vgmfile = row.file;

  switch (index.column()) {
    case Property::Name: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->name());
      } else if (role == Qt::DecorationRole) {
        if (auto *seq = dynamic_cast<VGMSeq *>(vgmfile)) {
          if (!seq->assocColls.empty()) {
            return sequenceInCollectionIcon();
          }
        }
        return iconForFile(vgmFileToVariant(vgmfile));
      }
      break;
    }

    case Property::Format: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->formatName());
      }
      break;
    }

    case Property::Type: {
      if (role == Qt::DisplayRole) {
        return typeLabelForFile(vgmfile);
      }
      break;
    }
    default:
      L_WARN("requested data for unexpected column index: {}", index.column());
      break;
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

    case Property::Type: {
      return "Type";
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

  return static_cast<int>(rows.size());
}

int VGMFileListModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return 3;
}

Qt::ItemFlags VGMFileListModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::ItemIsEnabled;
  }

  const auto &row = rows[static_cast<size_t>(index.row())];
  if (row.isHeader) {
    return Qt::ItemIsEnabled;
  }

  return QAbstractTableModel::flags(index);
}

void VGMFileListModel::sort(int column, Qt::SortOrder order) {
  if (column < 0 || column >= columnCount(QModelIndex())) {
    return;
  }

  beginResetModel();
  sortColumn = column;
  sortOrder = order;
  rebuildRows();
  endResetModel();
}

VGMFile *VGMFileListModel::fileFromIndex(const QModelIndex &index) const {
  if (!index.isValid()) {
    return nullptr;
  }

  const auto &row = rows[static_cast<size_t>(index.row())];
  if (row.isHeader) {
    return nullptr;
  }

  return row.file;
}

QModelIndex VGMFileListModel::indexFromFile(const VGMFile *file) const {
  if (!file) {
    return {};
  }

  for (size_t row = 0; row < rows.size(); ++row) {
    if (!rows[row].isHeader && rows[row].file == file) {
      return createIndex(static_cast<int>(row), 0);
    }
  }

  return {};
}

bool VGMFileListModel::isHeaderRow(const QModelIndex &index) const {
  if (!index.isValid()) {
    return false;
  }

  return rows[static_cast<size_t>(index.row())].isHeader;
}

void VGMFileListModel::rebuildRows() {
  rows.clear();

  struct Group {
    RawFile *raw = nullptr;
    std::vector<VGMFile *> files;
  };
  std::vector<Group> groups;

  for (const auto &variant : qtVGMRoot.vgmFiles()) {
    VGMFile *file = variantToVGMFile(variant);
    if (!file) {
      continue;
    }
    RawFile *raw = file->rawFile();
    auto it = std::find_if(groups.begin(), groups.end(), [raw](const Group &group) {
      return group.raw == raw;
    });
    if (it == groups.end()) {
      groups.push_back({raw, {}});
      it = groups.end() - 1;
    }
    it->files.push_back(file);
  }

  if (sortColumn >= 0) {
    for (auto &group : groups) {
      std::stable_sort(group.files.begin(), group.files.end(),
                       [this](VGMFile *lhs, VGMFile *rhs) {
                         const QString left = sortKeyForColumn(lhs, sortColumn);
                         const QString right = sortKeyForColumn(rhs, sortColumn);
                         if (sortOrder == Qt::AscendingOrder) {
                           return left < right;
                         }
                         return left > right;
                       });
    }
  }

  for (const auto &group : groups) {
    if (!group.raw) {
      continue;
    }
    rows.push_back({true, group.raw, nullptr});
    for (auto *file : group.files) {
      rows.push_back({false, group.raw, file});
    }
  }
}

/*
 *  VGMFileListView
 */

VGMFileListView::VGMFileListView(QWidget *parent) : TableView(parent) {
  view_model = new VGMFileListModel();
  VGMFileListView::setModel(view_model);

  setSortingEnabled(true);
  horizontalHeader()->setSortIndicatorShown(true);

  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  setContextMenuPolicy(Qt::CustomContextMenu);

  setColumnWidth(1, 140);
  setColumnWidth(2, 90);

  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::itemMenu);
  connect(this, &QAbstractItemView::doubleClicked, this, &VGMFileListView::requestVGMFileView);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &VGMFileListView::onVGMFileSelected);
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMFileListView::onSelectionChanged);
}

void VGMFileListView::itemMenu(const QPoint &pos) {
  if (!selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();

  auto selectedFiles = std::make_shared<std::vector<VGMFile*>>();
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      if (auto *file = view_model->fileFromIndex(index)) {
        selectedFiles->push_back(file);
      }
    }
  }
  if (selectedFiles->empty()) {
    return;
  }
  auto menu = MenuManager::the()->createMenuForItems<VGMItem>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMFileListView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void VGMFileListView::updateContextualMenus() const {
  if (!selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();
  QList<VGMFile*> files;
  files.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      if (auto *file = view_model->fileFromIndex(index)) {
        files.append(file);
      }
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMFiles(files);
}

void VGMFileListView::keyPressEvent(QKeyEvent *input) {
  switch (input->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
      if (currentIndex().isValid())
        requestVGMFileView(currentIndex());
      break;

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QTableView::keyPressEvent(input);
  }
}

void VGMFileListView::requestVGMFileView(const QModelIndex& index) {
  if (auto *file = view_model->fileFromIndex(index)) {
    MdiArea::the()->newView(file);
  }
}

// Update the status bar for the current selection
void VGMFileListView::updateStatusBar() const {
  if (!currentIndex().isValid()) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }
  VGMFile* file = view_model->fileFromIndex(currentIndex());
  NotificationCenter::the()->updateStatusForItem(file);
}

// Update the status bar on focus
void VGMFileListView::focusInEvent(QFocusEvent *event) {
  TableView::focusInEvent(event);
  updateStatusBar();
}

void VGMFileListView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
  TableView::currentChanged(current, previous);

  if (!current.isValid()) {
    NotificationCenter::the()->selectVGMFile(nullptr, this);
    return;
  }

  VGMFile *file = view_model->fileFromIndex(current);
  NotificationCenter::the()->selectVGMFile(file, this);

  if (this->hasFocus())
    updateStatusBar();
}

void VGMFileListView::onVGMFileSelected(VGMFile* file, const QWidget* caller) {
  if (caller == this)
    return;

  if (file == nullptr) {
    this->clearSelection();
    return;
  }

  QModelIndex firstIndex = view_model->indexFromFile(file);
  if (!firstIndex.isValid()) {
    return;
  }

  // Select the row corresponding to the file
  QModelIndex lastIndex = model()->index(firstIndex.row(), model()->columnCount() - 1); // Last column of the row

  if (firstIndex == currentIndex())
    return;

  QItemSelection selection(firstIndex, lastIndex);
  selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  // Prevent the currentChanged callback from triggering so that we don't recursively
  // call NotificationCenter::selectVGMFile()
  selectionModel()->blockSignals(true);
  setCurrentIndex(firstIndex);
  selectionModel()->blockSignals(false);

  scrollTo(firstIndex, QAbstractItemView::EnsureVisible);
}
