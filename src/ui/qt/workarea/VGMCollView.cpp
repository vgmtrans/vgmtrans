/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollView.h"

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

  /* Sum one because of the VGMColl sequence */
  return m_coll->instrsets.size() + m_coll->sampcolls.size() + m_coll->miscfiles.size() + 1;
}

QVariant VGMCollViewModel::data(const QModelIndex &index, int role) const {
  auto file = fileFromIndex(index);
  if (role == Qt::DisplayRole) {
    return QString::fromStdWString(*file->GetName());
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

  emit dataChanged(index(0, 0), index(0, 0));
}

VGMFile *VGMCollViewModel::fileFromIndex(QModelIndex index) const {
  auto row = index.row();
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
    : QListView(parent) {
  setAttribute(Qt::WA_MacShowFocusRect, 0);
  setIconSize(QSize(16, 16));

  VGMCollViewModel *vgmCollViewModel = new VGMCollViewModel(collListSelModel, this);
  setModel(vgmCollViewModel);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionRectVisible(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(this, &VGMCollView::doubleClicked, this, &VGMCollView::doubleClickedSlot);
}

void VGMCollView::doubleClickedSlot(QModelIndex index) {
  auto file_to_open = qobject_cast<VGMCollViewModel *>(model())->fileFromIndex(index);
  MdiArea::the()->newView(file_to_open);
}