/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PropertyView.h"

#include <algorithm>
#include <ranges>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QVBoxLayout>

#include <VGMColl.h>
#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMMiscFile.h>
#include <VGMSampColl.h>
#include <VGMSeq.h>

#include "Helpers.h"
#include "MdiArea.h"
#include "QtVGMRoot.h"
#include "SequencePlayer.h"
#include "services/MenuManager.h"
#include "services/NotificationCenter.h"

/*
 * PropertyViewModel
 */

PropertyViewModel::PropertyViewModel(QObject *parent) : QAbstractListModel(parent) {}

int PropertyViewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid() || !m_coll) {
    return 0;
  }

  return static_cast<int>(m_coll->miscFiles().size() + m_coll->instrSets().size() +
                          m_coll->sampColls().size());
}

QVariant PropertyViewModel::data(const QModelIndex &index, int role) const {
  auto *file = fileFromIndex(index);
  if (!file) {
    return {};
  }

  if (role == Qt::DisplayRole) {
    return QString::fromStdString(file->name());
  }
  if (role == Qt::DecorationRole) {
    return iconForFile(vgmFileToVariant(file));
  }

  return {};
}

void PropertyViewModel::setSequence(VGMSeq *seq) {
  VGMColl *next_coll = (seq && !seq->assocColls.empty()) ? seq->assocColls.front() : nullptr;
  if (seq == m_seq && next_coll == m_coll) {
    return;
  }

  beginResetModel();
  m_seq = seq;
  m_coll = next_coll;
  endResetModel();
}

VGMFile *PropertyViewModel::fileFromIndex(const QModelIndex &index) const {
  if (!m_coll || !index.isValid()) {
    return nullptr;
  }

  size_t row = static_cast<size_t>(index.row());
  const auto &misc_files = m_coll->miscFiles();
  if (row < misc_files.size()) {
    return misc_files[row];
  }

  row -= misc_files.size();
  const auto &instr_sets = m_coll->instrSets();
  if (row < instr_sets.size()) {
    return instr_sets[row];
  }

  row -= instr_sets.size();
  const auto &samp_colls = m_coll->sampColls();
  if (row < samp_colls.size()) {
    return samp_colls[row];
  }

  return nullptr;
}

QModelIndex PropertyViewModel::indexFromFile(const VGMFile *file) const {
  if (!m_coll || !file) {
    return {};
  }

  auto misc_it = std::ranges::find(m_coll->miscFiles(), file);
  if (misc_it != m_coll->miscFiles().end()) {
    return createIndex(static_cast<int>(std::distance(m_coll->miscFiles().begin(), misc_it)), 0);
  }

  int row = static_cast<int>(m_coll->miscFiles().size());
  auto instr_it = std::ranges::find(m_coll->instrSets(), file);
  if (instr_it != m_coll->instrSets().end()) {
    return createIndex(row + static_cast<int>(std::distance(m_coll->instrSets().begin(), instr_it)),
                       0);
  }

  row += static_cast<int>(m_coll->instrSets().size());
  auto samp_it = std::ranges::find(m_coll->sampColls(), file);
  if (samp_it != m_coll->sampColls().end()) {
    return createIndex(row + static_cast<int>(std::distance(m_coll->sampColls().begin(), samp_it)),
                       0);
  }

  return {};
}

bool PropertyViewModel::containsVGMFile(const VGMFile *file) const {
  if (!m_coll || !file) {
    return false;
  }

  return std::ranges::find(m_coll->miscFiles(), file) != m_coll->miscFiles().end() ||
         std::ranges::find(m_coll->instrSets(), file) != m_coll->instrSets().end() ||
         std::ranges::find(m_coll->sampColls(), file) != m_coll->sampColls().end();
}

/*
 * PropertyView
 */

PropertyView::PropertyView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  auto *header_layout = new QHBoxLayout();
  m_title = new QLabel("No sequence selected");
  m_edit_button = new QPushButton("Edit collection");
  m_edit_button->setEnabled(false);
  m_edit_button->setToolTip("Edit collection (coming soon)");
  m_edit_button->setAutoDefault(false);
  header_layout->addWidget(m_title, 1);
  header_layout->addWidget(m_edit_button);
  layout->addLayout(header_layout);

  m_listview = new QListView(this);
  m_listview->setAttribute(Qt::WA_MacShowFocusRect, false);
  m_listview->setIconSize(QSize(16, 16));
  m_listview->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_listview->setSelectionRectVisible(true);
  m_listview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_listview->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(m_listview, 1);

  m_model = new PropertyViewModel(this);
  m_listview->setModel(m_model);

  connect(m_listview, &QListView::doubleClicked, this, &PropertyView::doubleClickedSlot);
  connect(m_listview, &QAbstractItemView::customContextMenuRequested, this, &PropertyView::itemMenu);
  connect(m_listview->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &PropertyView::handleCurrentChanged);
  connect(m_listview->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &PropertyView::onSelectionChanged);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this,
          &PropertyView::onVGMFileSelected);
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedVGMColl, this, [this]() {
    if (m_current_seq) {
      setSelectedSequence(m_current_seq);
    }
  });
  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, [this](VGMColl *coll) {
    if (m_model->collection() == coll) {
      setSelectedSequence(m_current_seq);
    }
  });

  setLayout(layout);
}

void PropertyView::handlePlaybackRequest() {
  auto *coll = m_model->collection();
  if (!coll) {
    if (SequencePlayer::the().activeCollection() != nullptr) {
      SequencePlayer::the().toggle();
      return;
    }
    nothingToPlay();
    return;
  }

  SequencePlayer::the().playCollection(coll);
}

void PropertyView::handleStopRequest() {
  SequencePlayer::the().stop();
}

void PropertyView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return: {
      QModelIndex currentIndex = m_listview->currentIndex();
      if (currentIndex.isValid()) {
        MdiArea::the()->newView(m_model->fileFromIndex(currentIndex));
      }
      break;
    }
    case Qt::Key_Escape:
      handleStopRequest();
      break;
    default:
      QWidget::keyPressEvent(e);
  }
}

void PropertyView::itemMenu(const QPoint &pos) {
  const auto selectionModel = m_listview->selectionModel();
  if (!selectionModel->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel->selectedRows();
  auto selectedFiles = std::make_shared<std::vector<VGMFile *>>();
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedFiles->push_back(m_model->fileFromIndex(index));
    }
  }

  auto menu = MenuManager::the()->createMenuForItems<VGMItem>(selectedFiles);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void PropertyView::onSelectionChanged(const QItemSelection &, const QItemSelection &) {
  updateContextualMenus();
}

void PropertyView::onVGMFileSelected(VGMFile *file, const QWidget *caller) {
  if (caller == this) {
    return;
  }

  if (m_ignore_next_non_seq_selection) {
    m_ignore_next_non_seq_selection = false;
    if (file && !dynamic_cast<VGMSeq *>(file) && m_model->containsVGMFile(file)) {
      auto index = m_model->indexFromFile(file);
      if (index.isValid()) {
        m_listview->blockSignals(true);
        m_listview->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        m_listview->blockSignals(false);
      }
      return;
    }
  }

  auto *seq = dynamic_cast<VGMSeq *>(file);
  setSelectedSequence(seq);
}

void PropertyView::handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
  Q_UNUSED(previous);

  if (!current.isValid()) {
    NotificationCenter::the()->selectVGMFile(nullptr, this);
    return;
  }

  auto *file = m_model->fileFromIndex(current);
  if (!file) {
    return;
  }

  m_ignore_next_non_seq_selection = true;
  NotificationCenter::the()->selectVGMFile(file, this);
  if (this->hasFocus() || m_listview->hasFocus()) {
    NotificationCenter::the()->updateStatusForItem(file);
  }
}

void PropertyView::updateContextualMenus() const {
  if (!m_listview->selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
    return;
  }

  QModelIndexList list = m_listview->selectionModel()->selectedRows();
  QList<VGMFile *> files;
  files.reserve(list.size());

  for (const auto &index : list) {
    if (index.isValid()) {
      files.append(m_model->fileFromIndex(index));
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMFiles(files);
}

void PropertyView::updateHeader() {
  if (!m_current_seq) {
    m_title->setText("No sequence selected");
    m_listview->setEnabled(false);
    m_edit_button->setEnabled(false);
    return;
  }

  auto *coll = m_model->collection();
  if (!coll) {
    m_title->setText("No collection for selected sequence");
    m_listview->setEnabled(false);
    m_edit_button->setEnabled(false);
    return;
  }

  m_title->setText(QString("Collection: %1").arg(QString::fromStdString(coll->name())));
  m_listview->setEnabled(true);
  m_edit_button->setEnabled(false);
}

void PropertyView::setSelectedSequence(VGMSeq *seq) {
  m_current_seq = seq;
  m_model->setSequence(seq);
  m_listview->selectionModel()->clearSelection();
  updateHeader();
}

void PropertyView::doubleClickedSlot(const QModelIndex &index) const {
  if (!index.isValid()) {
    return;
  }
  auto *file_to_open = m_model->fileFromIndex(index);
  MdiArea::the()->newView(file_to_open);
}
