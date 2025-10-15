/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollView.h"

#include <QVBoxLayout>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QKeyEvent>

#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>
#include <VGMMiscFile.h>
#include <VGMColl.h>
#include "QtVGMRoot.h"
#include "Helpers.h"
#include "MdiArea.h"
#include "services/NotificationCenter.h"
#include "services/MenuManager.h"

VGMCollViewModel::VGMCollViewModel(const QItemSelectionModel *collListSelModel, QObject *parent)
    : QAbstractListModel(parent), m_coll(nullptr) {
  QObject::connect(collListSelModel, &QItemSelectionModel::currentChanged, this,
                   &VGMCollViewModel::handleNewCollSelected);
}

int VGMCollViewModel::rowCount(const QModelIndex &parent) const {
  if (m_coll == nullptr) {
    return 0;
  }
  // plus one because of the VGMColl sequence
  return static_cast<int>(m_coll->instrSets().size() + m_coll->sampColls().size() +
                          m_coll->miscFiles().size() + 1);
}

QVariant VGMCollViewModel::data(const QModelIndex &index, int role) const {
  auto file = fileFromIndex(index);
  if (!file) {
    return QIcon{":/icons/file.svg"};
  }

  if (role == Qt::DisplayRole) {
    return QString::fromStdString(file->name());
  } else if (role == Qt::DecorationRole) {
    return iconForFile(vgmFileToVariant(file));
  }

  return QVariant();
}

void VGMCollViewModel::handleNewCollSelected(const QModelIndex& modelIndex) {
  if (!modelIndex.isValid() || qtVGMRoot.vgmColls().empty() ||
      static_cast<size_t>(modelIndex.row()) >= qtVGMRoot.vgmColls().size()) {
    m_coll = nullptr;
  } else {
    m_coll = qtVGMRoot.vgmColls()[modelIndex.row()];
  }

  dataChanged(index(0, 0), modelIndex);
}

VGMFile *VGMCollViewModel::fileFromIndex(const QModelIndex& index) const {
  if (!m_coll)
    return nullptr;

  size_t row = index.row();
  auto num_instrsets = m_coll->instrSets().size();
  auto num_sampcolls = m_coll->sampColls().size();
  auto num_miscfiles = m_coll->miscFiles().size();

  if (row < num_miscfiles) {
    return m_coll->miscFiles()[row];
  }

  row -= num_miscfiles;
  if (row < num_instrsets) {
    return m_coll->instrSets()[row];
  }

  row -= num_instrsets;
  if (row < num_sampcolls) {
    return m_coll->sampColls()[row];
  } else {
    return m_coll->seq();
  }
}

QModelIndex VGMCollViewModel::indexFromFile(const VGMFile* file) const {
  int row = 0;

  // Check in miscfiles
  auto miscIt = std::ranges::find(m_coll->miscFiles(), file);
  if (miscIt != m_coll->miscFiles().end()) {
    return createIndex(static_cast<int>(std::distance(m_coll->miscFiles().begin(), miscIt)), 0);
  }
  row += m_coll->miscFiles().size();

  // Check in instrsets
  auto instrIt = std::ranges::find(m_coll->instrSets(), file);
  if (instrIt != m_coll->instrSets().end()) {
    return createIndex(row + static_cast<int>(std::distance(m_coll->instrSets().begin(), instrIt)), 0);
  }
  row += m_coll->instrSets().size();

  // Check in sampcolls
  auto sampIt = std::ranges::find(m_coll->sampColls(), file);
  if (sampIt != m_coll->sampColls().end()) {
    return createIndex(row + static_cast<int>(std::distance(m_coll->sampColls().begin(), sampIt)), 0);
  }
  row += m_coll->sampColls().size();

  // Check if it's seq
  if (m_coll->seq() == file) {
    return createIndex(row, 0);
  }

  // If not found, return an invalid QModelIndex
  return QModelIndex();
}

bool VGMCollViewModel::containsVGMFile(const VGMFile* file) const {
  if (!m_coll)
    return false;
  return m_coll->containsVGMFile(file);
}

void VGMCollViewModel::removeVGMColl(const VGMColl* coll) {
  if (m_coll != coll)
    return;

  // Select an invalid index to clear the view
  handleNewCollSelected(QModelIndex());
}


VGMCollView::VGMCollView(QItemSelectionModel *collListSelModel, QWidget *parent)
    : QGroupBox("Selected collection", parent) {
  auto layout = new QVBoxLayout();

  auto rename_layout = new QHBoxLayout();
  m_collection_title = new QLineEdit("No collection selected");
  m_collection_title->setReadOnly(true);
  rename_layout->addWidget(m_collection_title);

  auto commit_rename = new QPushButton("Rename");
  commit_rename->setEnabled(false);
  commit_rename->setAutoDefault(true);
  commit_rename->setIcon(QIcon(":/icons/collection.svg"));
  rename_layout->addWidget(commit_rename);
  layout->addLayout(rename_layout);

  m_listview = new QListView(this);
  m_listview->setAttribute(Qt::WA_MacShowFocusRect, false);
  m_listview->setIconSize(QSize(16, 16));
  layout->addWidget(m_listview);

  vgmCollViewModel = new VGMCollViewModel(collListSelModel, this);
  m_listview->setModel(vgmCollViewModel);
  m_listview->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_listview->setSelectionRectVisible(true);
  m_listview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  setContextMenuPolicy(Qt::CustomContextMenu);

  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, &VGMCollView::removeVGMColl);
  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMCollView::itemMenu);
  connect(m_listview, &QListView::doubleClicked, this, &VGMCollView::doubleClickedSlot);
  connect(m_listview->selectionModel(), &QItemSelectionModel::currentChanged, this, &VGMCollView::handleCurrentChanged);
  connect(m_listview->selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMCollView::onSelectionChanged);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &VGMCollView::onVGMFileSelected);

  QObject::connect(collListSelModel, &QItemSelectionModel::currentChanged,
    [this, commit_rename](const QModelIndex& index) {
    if (!index.isValid() || qtVGMRoot.vgmColls().empty() ||
        static_cast<size_t>(index.row()) >= qtVGMRoot.vgmColls().size()) {
      commit_rename->setEnabled(false);
      m_collection_title->setReadOnly(true);
      m_collection_title->setText("No collection selected");
    } else {
      m_collection_title->setText(
          QString::fromStdString(qtVGMRoot.vgmColls()[index.row()]->name()));
      m_collection_title->setReadOnly(false);
      commit_rename->setEnabled(true);
    }
  });

  QObject::connect(commit_rename, &QPushButton::pressed, [this, collListSelModel]() {
    auto model_index = collListSelModel->currentIndex();
    if (!model_index.isValid() || qtVGMRoot.vgmColls().empty() ||
        model_index.row() >= qtVGMRoot.vgmColls().size()) {
      return;
    }

    auto title = m_collection_title->text().toStdString();
    /* This makes a copy, no worries */
    qtVGMRoot.vgmColls()[model_index.row()]->setName(title);

    auto coll_list_model = collListSelModel->model();
    coll_list_model->dataChanged(coll_list_model->index(0, 0),
                                 coll_list_model->index(model_index.row(), 0));
  });

  setLayout(layout);
}

void VGMCollView::itemMenu(const QPoint &pos) {
  const auto selectionModel = m_listview->selectionModel();
  if (!selectionModel->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel->selectedRows();

  auto selectedFiles = std::make_shared<std::vector<VGMFile*>>();
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedFiles->push_back(vgmCollViewModel->fileFromIndex(index));
    }
  }
  auto menu = MenuManager::the()->createMenuForItems<VGMItem>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMCollView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return: {
      QModelIndex currentIndex = m_listview->currentIndex();
      if (currentIndex.isValid()) {
        auto model = qobject_cast<VGMCollViewModel *>(m_listview->model());
        MdiArea::the()->newView(model->fileFromIndex(currentIndex));
      }
      break;
    }
    default:
      QGroupBox::keyPressEvent(e);
  }
}

void VGMCollView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void VGMCollView::updateContextualMenus() const {
  if (!m_listview->selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
    return;
  }

  QModelIndexList list = m_listview->selectionModel()->selectedRows();
  QList<VGMFile*> files;
  files.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      files.append(vgmCollViewModel->fileFromIndex(index));
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMFiles(files);
}

void VGMCollView::removeVGMColl(const VGMColl *coll) const {
  if (vgmCollViewModel->m_coll != coll)
    return;

  m_collection_title->setText("No collection selected");
  vgmCollViewModel->removeVGMColl(coll);
}

void VGMCollView::doubleClickedSlot(const QModelIndex& index) const {
  auto file_to_open = qobject_cast<VGMCollViewModel *>(m_listview->model())->fileFromIndex(index);
  MdiArea::the()->newView(file_to_open);
}

void VGMCollView::handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
  Q_UNUSED(previous);

  if (!current.isValid()) {
    NotificationCenter::the()->selectVGMFile(nullptr, this);
    return;
  }

  VGMFile *file = vgmCollViewModel->fileFromIndex(current);
  NotificationCenter::the()->selectVGMFile(file, this);
  if (this->hasFocus() || m_listview->hasFocus())
    NotificationCenter::the()->updateStatusForItem(file);
}

void VGMCollView::onVGMFileSelected(const VGMFile *file, const QWidget* caller) const {
  if (caller == this)
    return;

  if (file == nullptr) {
    m_listview->clearSelection();
    return;
  }

  if (!vgmCollViewModel->containsVGMFile(file)) {
    m_listview->selectionModel()->clearSelection();
    return;
  }
  auto index = vgmCollViewModel->indexFromFile(file);

  // Select the row corresponding to the file and block signals to prevent a recursive
  // call to NotificationCenter::selectVGMFile()
  m_listview->blockSignals(true);
  m_listview->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
  m_listview->blockSignals(false);
  m_listview->scrollTo(index, QAbstractItemView::EnsureVisible);
}
