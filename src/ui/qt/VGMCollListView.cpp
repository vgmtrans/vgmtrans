/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QEvent>
#include <QMenu>
#include <QLineEdit>
#include "VGMCollListView.h"
#include "QtVGMRoot.h"
#include "VGMColl.h"
#include "MusicPlayer.h"

/*
 * VGMCollListViewModel
 */
VGMCollListViewModel::VGMCollListViewModel(QObject *parent) : QAbstractListModel(parent) {
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMColl,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedVGMColl,
          [=]() { dataChanged(index(0, 0), index(0, 0)); });
}

int VGMCollListViewModel::rowCount(const QModelIndex &parent) const {
  return qtVGMRoot.vVGMColl.size();
}

QVariant VGMCollListViewModel::data(const QModelIndex &index, int role) const {
  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    return QString::fromStdWString(*qtVGMRoot.vVGMColl[index.row()]->GetName());
  } else if (role == Qt::DecorationRole) {
    return QIcon(":/images/collection-32.png");
  }

  return QVariant();
}

Qt::ItemFlags VGMCollListViewModel::flags(const QModelIndex &index) const {
  if (!index.isValid())
    return Qt::ItemIsEnabled;

  return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

/*
 * VGMCollNameEditor
 */

void VGMCollNameEditor::setEditorData(QWidget *editor, const QModelIndex &index) const {
  QString orig_name = index.model()->data(index, Qt::EditRole).toString();
  QLineEdit *line_edit = dynamic_cast<QLineEdit *>(editor);
  line_edit->setText(orig_name);
}

void VGMCollNameEditor::setModelData(QWidget *editor, QAbstractItemModel *model,
                                     const QModelIndex &index) const {
  QLineEdit *line_edit = dynamic_cast<QLineEdit *>(editor);
  auto new_name = line_edit->text().toStdWString();
  qtVGMRoot.vVGMColl[index.row()]->SetName(&new_name);
  model->dataChanged(index, index);
}

/*
 * VGMCollListView
 */

VGMCollListView::VGMCollListView(QWidget *parent) : QListView(parent) {
  setModel(new VGMCollListViewModel);
  setItemDelegate(new VGMCollNameEditor);

  setContextMenuPolicy(Qt::CustomContextMenu);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::DoubleClicked);
  setResizeMode(QListView::Adjust);
  setIconSize(QSize(16, 16));
  setWrapping(true);

  connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMCollListView::CollMenu);
}

void VGMCollListView::CollMenu(const QPoint &pos) {
  auto element = indexAt(pos);
  if (element.isValid()) {
    VGMColl *pointed_coll = qtVGMRoot.vVGMColl[element.row()];
    if (pointed_coll == nullptr) {
      return;
    }

    QMenu *vgmcoll_menu = new QMenu();
    std::vector<const wchar_t *> *menu_item_names = pointed_coll->GetMenuItemNames();
    for (auto &menu_item : *menu_item_names) {
      vgmcoll_menu->addAction(QString::fromStdWString(menu_item));
    }

    QAction *performed_action = vgmcoll_menu->exec(mapToGlobal(pos));
    int action_index = 0;
    for (auto &action : vgmcoll_menu->actions()) {
      if (performed_action == action) {
        pointed_coll->CallMenuItem(pointed_coll, action_index);
        break;
      }
      action_index++;
    }
    vgmcoll_menu->deleteLater();
  } else if (qtVGMRoot.vVGMColl.size() > 0) {
    QMenu *vgmcoll_menu = new QMenu();
    auto export_all = vgmcoll_menu->addAction("Export all as MIDI, SF2 and DLS");

    if (vgmcoll_menu->exec(mapToGlobal(pos)) == export_all) {
      auto save_path = qtVGMRoot.UI_GetSaveDirPath();
      if(!save_path.empty()) {
        for (auto &coll : qtVGMRoot.vVGMColl) {
          coll->SetDefaultSavePath(save_path);
          coll->OnSaveAll();
        }
      }
    }
    vgmcoll_menu->deleteLater();
  }
}

void VGMCollListView::keyPressEvent(QKeyEvent* e) {
  switch(e->key()) {
    case Qt::Key_Space: {
      HandlePlaybackRequest();
      break;
    }

    case Qt::Key_Escape : {
      HandleStopRequest();
      break;
    }

    default:
      QListView::keyPressEvent(e);
  }
}

void VGMCollListView::HandlePlaybackRequest() {
  QModelIndexList list = selectionModel()->selectedIndexes();
  if (list.empty() || list[0].row() >= qtVGMRoot.vVGMColl.size())
    return;

  MusicPlayer& player = MusicPlayer::Instance();
  player.LoadCollection(qtVGMRoot.vVGMColl[list[0].row()]);
  player.Toggle();
}

void VGMCollListView::HandleStopRequest() {
  MusicPlayer& player = MusicPlayer::Instance();
  player.Stop();
}
