/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QEvent>
#include <QMenu>
#include <QLineEdit>
#include <QObject>
#include <MusicPlayer.h>
#include <VGMColl.h>
#include <VGMExport.h>
#include "VGMCollListView.h"
#include "../QtVGMRoot.h"

#include <SF2File.h>
#include <MidiFile.h>
#include <VGMSeq.h>

static const QIcon &VGMCollIcon() {
  static QIcon icon(":/images/collection.svg");
  return icon;
}

/*
 * VGMCollListViewModel
 */
VGMCollListViewModel::VGMCollListViewModel(QObject *parent) : QAbstractListModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMColl,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedVGMColl,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
}

int VGMCollListViewModel::rowCount(const QModelIndex &) const {
  return qtVGMRoot.vVGMColl.size();
}

QVariant VGMCollListViewModel::data(const QModelIndex &index, int role) const {
  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    return QString::fromStdWString(*qtVGMRoot.vVGMColl[index.row()]->GetName());
  } else if (role == Qt::DecorationRole) {
    return VGMCollIcon();
  }

  return QVariant();
}

Qt::ItemFlags VGMCollListViewModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::ItemIsEnabled;
  }

  return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

/*
 * VGMCollNameEditor
 */

void VGMCollNameEditor::setEditorData(QWidget *editor, const QModelIndex &index) const {
  QString orig_name = index.model()->data(index, Qt::EditRole).toString();
  auto *line_edit = qobject_cast<QLineEdit *>(editor);
  line_edit->setText(orig_name);
}

void VGMCollNameEditor::setModelData(QWidget *editor, QAbstractItemModel *model,
                                     const QModelIndex &index) const {
  auto *line_edit = qobject_cast<QLineEdit *>(editor);
  auto new_name = line_edit->text().toStdWString();
  qtVGMRoot.vVGMColl[index.row()]->SetName(&new_name);
  model->dataChanged(index, index);
}

/*
 * VGMCollListView
 */

VGMCollListView::VGMCollListView(QWidget *parent) : QListView(parent) {
  auto model = new VGMCollListViewModel;
  setModel(model);
  setItemDelegate(new VGMCollNameEditor);

  setContextMenuPolicy(Qt::CustomContextMenu);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setEditTriggers(QAbstractItemView::DoubleClicked);
  setResizeMode(QListView::Adjust);
  setIconSize(QSize(16, 16));
  setWrapping(true);

  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMCollListView::CollMenu);
  connect(model, &VGMCollListViewModel::dataChanged, [=]() {
    if (!selectedIndexes().empty()) {
      selectionModel()->currentChanged(selectedIndexes().front(), {});
    } else {
      selectionModel()->currentChanged({}, {});
    }
  });
}

void VGMCollListView::CollMenu(const QPoint &pos) {
  if (selectedIndexes().empty()) {
    return;
  }

  auto *vgmcoll_menu = new QMenu();
  vgmcoll_menu->addAction("Export as MIDI and DLS", [this]() {
    auto save_path = qtVGMRoot.UI_GetSaveDirPath();
    if (save_path.empty()) {
      return;
    }

    for (auto &index : selectedIndexes()) {
      if (auto coll = qtVGMRoot.vVGMColl[index.row()]; coll) {
        SaveAs<VGMCollConversionTarget::MIDI | VGMCollConversionTarget::DLS>(*coll, save_path);
      }
    }
  });

  vgmcoll_menu->addAction("Export as MIDI and SF2", [this]() {
    auto save_path = qtVGMRoot.UI_GetSaveDirPath();
    if (save_path.empty()) {
      return;
    }

    for (auto &index : selectedIndexes()) {
      if (auto coll = qtVGMRoot.vVGMColl[index.row()]; coll) {
        SaveAs<VGMCollConversionTarget::MIDI | VGMCollConversionTarget::SF2>(*coll, save_path);
      }
    }
  });

  vgmcoll_menu->addAction("Export as MIDI, DLS and SF2", [this]() {
    auto save_path = qtVGMRoot.UI_GetSaveDirPath();
    if (save_path.empty()) {
      return;
    }

    for (auto &index : selectedIndexes()) {
      if (auto coll = qtVGMRoot.vVGMColl[index.row()]; coll) {
        SaveAs<VGMCollConversionTarget::MIDI | VGMCollConversionTarget::DLS | VGMCollConversionTarget::SF2>(*coll, save_path);
      }
    }
  });

  vgmcoll_menu->exec(mapToGlobal(pos));
  vgmcoll_menu->deleteLater();
}

void VGMCollListView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Space: {
      HandlePlaybackRequest();
      break;
    }

    case Qt::Key_Escape: {
      HandleStopRequest();
      break;
    }

    default:
      QListView::keyPressEvent(e);
  }
}

void VGMCollListView::HandlePlaybackRequest() {
  QModelIndexList list = this->selectionModel()->selectedIndexes();
  if (list.empty() || list[0].row() >= qtVGMRoot.vVGMColl.size()) {
    return;
  }

  //todo: move somewhere else
  VGMColl *coll = qtVGMRoot.vVGMColl[list[0].row()];
  VGMSeq *seq = coll->GetSeq();
  if (!seq) {
    return;
  }

  SF2File *sf2 = coll->CreateSF2File();
  if (!sf2) {
    return;
  }
  
  MidiFile *midi = seq->ConvertToMidi();

  std::vector<uint8_t> midiBuf;
  midi->WriteMidiToBuffer(midiBuf);

  void *rawSF2 = const_cast<void *>(sf2->SaveToMem());

  common::MusicPlayer::the().loadDataAndPlay(
      gsl::make_span(static_cast<char *>(rawSF2), sf2->GetSize()),
      gsl::make_span(reinterpret_cast<char *>(midiBuf.data()), midiBuf.size()));

  delete[] rawSF2;
  delete sf2;
  delete midi;
}

void VGMCollListView::HandleStopRequest() {
  common::MusicPlayer::the().stop();
}