/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <ranges>

#include <QHeaderView>

#include "VGMFileListView.h"
#include "Helpers.h"
#include "QtVGMRoot.h"
#include "MdiArea.h"
#include "services/NotificationCenter.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMSeq.h"
#include "VGMMiscFile.h"
#include "VGMSampColl.h"
#include "services/MenuManager.h"

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

  VGMFileVariant vgmfilevariant = qtVGMRoot.vgmFiles().at(index.row());
  VGMFile* vgmfile = variantToVGMFile(vgmfilevariant);

  switch (index.column()) {
    case Property::Name: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->name());
      } else if (role == Qt::DecorationRole) {
        return iconForFile(vgmfilevariant);
      }
      break;
    }

    case Property::Format: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->formatName());
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

    default: {
      return {};
    }
  }
}

int VGMFileListModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(qtVGMRoot.vgmFiles().size());
}

int VGMFileListModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return 2;
}

void VGMFileListModel::AddVGMFile() {
  int position = static_cast<int>(qtVGMRoot.vgmFiles().size()) - 1;
  beginInsertRows(QModelIndex(), position, position);
  endInsertRows();
}

void VGMFileListModel::RemoveVGMFile() {
  int position = static_cast<int>(qtVGMRoot.vgmFiles().size()) - 1;
  if (position < 0) {
    return;
  }

  beginRemoveRows(QModelIndex(), position, position);
  endRemoveRows();
}

/*
 *  VGMFileListView
 */

VGMFileListView::VGMFileListView(QWidget *parent) : TableView(parent) {
  view_model = new VGMFileListModel();
  VGMFileListView::setModel(view_model);

  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  setContextMenuPolicy(Qt::CustomContextMenu);

  connect(&qtVGMRoot, &QtVGMRoot::UI_RemoveVGMFile, this, &VGMFileListView::removeVGMFile);
  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::itemMenu);
  connect(this, &QAbstractItemView::doubleClicked, this, &VGMFileListView::requestVGMFileView);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &VGMFileListView::onVGMFileSelected);
}

void VGMFileListView::itemMenu(const QPoint &pos) {
  auto selectedFiles = std::make_shared<std::vector<VGMFile*>>();

  if (!selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();

  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedFiles->push_back(variantToVGMFile(qtVGMRoot.vgmFiles()[index.row()]));
    }
  }
  auto menu = MenuManager::the()->CreateMenuForItems<VGMItem>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMFileListView::keyPressEvent(QKeyEvent *input) {
  switch (input->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
      if (currentIndex().isValid())
        requestVGMFileView(currentIndex());
      break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
      if (!selectionModel()->hasSelection())
        return;

      QModelIndexList list = selectionModel()->selectedRows();
      pRoot->UI_BeginRemoveVGMFiles();
      for (auto & idx : std::ranges::reverse_view(list)) {
        qtVGMRoot.RemoveVGMFile(qtVGMRoot.vgmFiles()[idx.row()], true);
      }
      pRoot->UI_EndRemoveVGMFiles();

      clearSelection();
      return;
    }

    // Pass the event back to the base class, needed for keyboard navigation
    default:
      QTableView::keyPressEvent(input);
  }
}

void VGMFileListView::removeVGMFile(const VGMFile *file) const {
  MdiArea::the()->removeView(file);
  view_model->RemoveVGMFile();
}

void VGMFileListView::requestVGMFileView(const QModelIndex& index) {
  MdiArea::the()->newView(variantToVGMFile(qtVGMRoot.vgmFiles()[index.row()]));
}

// Update the status bar for the current selection
void VGMFileListView::updateStatusBar() const {
  if (!currentIndex().isValid()) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }
  VGMFile* file = variantToVGMFile(qtVGMRoot.vgmFiles()[currentIndex().row()]);
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

  VGMFile *file = variantToVGMFile(qtVGMRoot.vgmFiles()[current.row()]);
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

  auto it = std::ranges::find_if(qtVGMRoot.vgmFiles(), [&](const VGMFileVariant& var) {
      // Use std::visit to access the actual object within the variant
      bool matches = false;

      std::visit([&](auto&& arg) {
        matches = (file == arg);
      }, var);

      return matches;
    });
  if (it == qtVGMRoot.vgmFiles().end())
    return;
  int row = static_cast<int>(std::distance(qtVGMRoot.vgmFiles().begin(), it));

  // Select the row corresponding to the file
  QModelIndex firstIndex = model()->index(row, 0); // First column of the row
  QModelIndex lastIndex = model()->index(row, model()->columnCount() - 1); // Last column of the row

  if (firstIndex == currentIndex())
    return;

  QItemSelection selection(firstIndex, lastIndex);
  selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  setCurrentIndex(firstIndex);

  scrollTo(firstIndex, QAbstractItemView::EnsureVisible);
}