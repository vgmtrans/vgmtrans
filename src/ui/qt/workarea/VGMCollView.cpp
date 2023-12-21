/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollView.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>

#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>
#include <VGMMiscFile.h>
#include <VGMColl.h>
#include "QtVGMRoot.h"
#include "Helpers.h"
#include "MdiArea.h"

VGMCollViewModel::VGMCollViewModel(QItemSelectionModel *collListSelModel, QObject *parent)
    : QAbstractListModel(parent), m_coll(nullptr) {
  QObject::connect(collListSelModel, &QItemSelectionModel::currentChanged, this,
                   &VGMCollViewModel::handleNewCollSelected);
}

int VGMCollViewModel::rowCount(const QModelIndex &parent) const {
  if (m_coll == nullptr) {
    return 0;
  }
  // plus one because of the VGMColl sequence
  return static_cast<int>(m_coll->instrsets.size() + m_coll->sampcolls.size() +
                          m_coll->miscfiles.size() + 1);
}

QVariant VGMCollViewModel::data(const QModelIndex &index, int role) const {
  auto file = fileFromIndex(index);
  if (!file) {
    return iconForFileType(FileType::FILETYPE_UNDEFINED);
  }

  if (role == Qt::DisplayRole) {
    return QString::fromStdString(*file->GetName());
  } else if (role == Qt::DecorationRole) {
    return iconForFileType(file->GetFileType());
  }

  return QVariant();
}

void VGMCollViewModel::handleNewCollSelected(QModelIndex modelIndex) {
  if (!modelIndex.isValid() || qtVGMRoot.vVGMColl.empty() ||
      modelIndex.row() >= qtVGMRoot.vVGMColl.size()) {
    m_coll = nullptr;
  } else {
    m_coll = qtVGMRoot.vVGMColl[modelIndex.row()];
  }

  dataChanged(index(0, 0), modelIndex);
}

VGMFile *VGMCollViewModel::fileFromIndex(QModelIndex index) const {
  size_t row = index.row();
  auto num_instrsets = m_coll->instrsets.size();
  auto num_sampcolls = m_coll->sampcolls.size();
  auto num_miscfiles = m_coll->miscfiles.size();

  if (row < num_miscfiles) {
    return m_coll->miscfiles[row];
  }

  row -= num_miscfiles;
  if (row < num_instrsets) {
    return m_coll->instrsets[row];
  }

  row -= num_instrsets;
  if (row < num_sampcolls) {
    return m_coll->sampcolls[row];
  } else {
    return m_coll->seq;
  }
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
  commit_rename->setIcon(QIcon(":/images/collection.svg"));
  rename_layout->addWidget(commit_rename);
  layout->addLayout(rename_layout);

  m_listview = new QListView(this);
  m_listview->setAttribute(Qt::WA_MacShowFocusRect, 0);
  m_listview->setIconSize(QSize(16, 16));
  layout->addWidget(m_listview);

  VGMCollViewModel *vgmCollViewModel = new VGMCollViewModel(collListSelModel, this);
  m_listview->setModel(vgmCollViewModel);
  m_listview->setSelectionMode(QAbstractItemView::SingleSelection);
  m_listview->setSelectionRectVisible(true);
  m_listview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(m_listview, &QListView::doubleClicked, this, &VGMCollView::doubleClickedSlot);

  QObject::connect(collListSelModel, &QItemSelectionModel::currentChanged, [=](QModelIndex index) {
    if (!index.isValid() || qtVGMRoot.vVGMColl.empty() ||
        index.row() >= qtVGMRoot.vVGMColl.size()) {
      commit_rename->setEnabled(false);
      m_collection_title->setReadOnly(true);
      m_collection_title->setText("No collection selected");
    } else {
      m_collection_title->setText(
          QString::fromStdString(*qtVGMRoot.vVGMColl[index.row()]->GetName()));
      m_collection_title->setReadOnly(false);
      commit_rename->setEnabled(true);
    }
  });

  QObject::connect(commit_rename, &QPushButton::pressed, [=]() {
    auto model_index = collListSelModel->currentIndex();
    if (!model_index.isValid() || qtVGMRoot.vVGMColl.empty() ||
        model_index.row() >= qtVGMRoot.vVGMColl.size()) {
      return;
    }

    auto title = m_collection_title->text().toStdString();
    /* This makes a copy, no worries */
    qtVGMRoot.vVGMColl[model_index.row()]->SetName(&title);

    auto coll_list_model = collListSelModel->model();
    coll_list_model->dataChanged(coll_list_model->index(0, 0),
                                 coll_list_model->index(model_index.row(), 0));
  });

  setLayout(layout);
}

void VGMCollView::doubleClickedSlot(QModelIndex index) {
  auto file_to_open = qobject_cast<VGMCollViewModel *>(m_listview->model())->fileFromIndex(index);
  MdiArea::the()->newView(file_to_open);
}
