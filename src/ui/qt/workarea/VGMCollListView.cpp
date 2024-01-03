/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollListView.h"

#include <algorithm>
#include <QLineEdit>
#include <VGMColl.h>
#include <VGMExport.h>
#include <VGMSeq.h>
#include "workarea/MdiArea.h"
#include "services/playerservice/PlayerService.h"
#include "QtVGMRoot.h"
#include "services/MenuManager.h"
#include "services/NotificationCenter.h"

static const QIcon &VGMCollIcon() {
  static QIcon icon(":/icons/collection.svg");
  return icon;
}

/*
 * VGMCollListViewModel
 */
VGMCollListViewModel::VGMCollListViewModel(QObject *parent) : QAbstractListModel(parent) {
  auto startResettingModel = [this]() { beginResetModel(); };
  auto endResettingModel = [this]() {
    endResetModel();
    NotificationCenter::the()->updateContextualMenusForVGMColls({});
  };

  auto beginLoad = [this]() {
    isLoadingRawFile = true;
    collsBeforeLoad = pRoot->vgmColls().size();
  };

  auto endLoad = [this]() {
    int filesLoaded = pRoot->vgmColls().size() - collsBeforeLoad;
    if (filesLoaded <= 0) {
      isLoadingRawFile = false;
      return;
    }
    beginInsertRows(QModelIndex(), collsBeforeLoad, collsBeforeLoad + filesLoaded - 1);
    endInsertRows();
    isLoadingRawFile = false;
  };

  auto addCollection = [this]() {
    if (isLoadingRawFile)
      return;

    int newIndex = static_cast<int>(pRoot->vgmColls().size()) - 1;
    if (newIndex < 0)
      return;

    beginInsertRows(QModelIndex(), newIndex, newIndex);
    endInsertRows();
  };


  connect(&qtVGMRoot, &QtVGMRoot::UI_beginLoadRawFile, beginLoad);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endLoadRawFile, endLoad);
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedVGMColl, addCollection);
  connect(&qtVGMRoot, &QtVGMRoot::UI_beginRemoveVGMColls, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveVGMColls, endResettingModel);
}

int VGMCollListViewModel::rowCount(const QModelIndex &) const {
  return static_cast<int>(qtVGMRoot.vgmColls().size());
}

QVariant VGMCollListViewModel::data(const QModelIndex &index, int role) const {
  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    return QString::fromStdString(qtVGMRoot.vgmColls()[index.row()]->name());
  } else if (role == Qt::DecorationRole) {
    return VGMCollIcon();
  }

  return QVariant();
}

Qt::ItemFlags VGMCollListViewModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::ItemIsEnabled;
  }

  return QAbstractListModel::flags(index);
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
  auto new_name = line_edit->text().toStdString();
  qtVGMRoot.vgmColls()[index.row()]->setName(new_name);
  model->dataChanged(index, index);
}

/*
 * VGMCollListView
 */

VGMCollListView::VGMCollListView(QWidget *parent) : QListView(parent) {
  auto model = new VGMCollListViewModel(this);
  VGMCollListView::setModel(model);
  setItemDelegate(new VGMCollNameEditor);

  setContextMenuPolicy(Qt::CustomContextMenu);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setResizeMode(QListView::Adjust);
  setIconSize(QSize(16, 16));
  setWrapping(true);

#ifdef Q_OS_MAC
  // On MacOS, a wrapping QListView gives unwanted padding to the scrollbar. This compensates.
  int scrollBarThickness = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
  QMargins margins = viewportMargins();
  margins.setBottom(margins.bottom() - scrollBarThickness);
  setViewportMargins(margins);
#endif

  connect(this, &QListView::doubleClicked, this,
          &VGMCollListView::handlePlaybackRequest);
  connect(this, &QAbstractItemView::customContextMenuRequested, this,
          &VGMCollListView::collectionMenu);
  connect(model, &VGMCollListViewModel::dataChanged, [this]() {
    if (!selectedIndexes().empty()) {
      selectionModel()->currentChanged(selectedIndexes().front(), {});
    } else {
      selectionModel()->currentChanged({}, {});
    }
  });
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMCollListView::onSelectionChanged);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this,
          &VGMCollListView::onVGMFileSelected);
}

void VGMCollListView::collectionMenu(const QPoint &pos) const {
  if (selectedIndexes().empty()) {
    return;
  }

  if (!selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();

  auto selectedColls = std::make_shared<std::vector<VGMColl*>>();
  selectedColls->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      selectedColls->push_back(qtVGMRoot.vgmColls()[index.row()]);
    }
  }
  auto menu = MenuManager::the()->createMenuForItems<VGMColl>(selectedColls);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMCollListView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
      handlePlaybackRequest();
      break;
    case Qt::Key_Escape:
      handleStopRequest();
      break;
    default:
      QListView::keyPressEvent(e);
  }
}

void VGMCollListView::handlePlaybackRequest() {
  QModelIndexList list = this->selectionModel()->selectedIndexes();
  if (list.empty() || list[0].row() >= model()->rowCount()) {
    if (PlayerService::getInstance().activeCollection() != nullptr) {
      PlayerService::getInstance().toggle();
      return;
    }
    nothingToPlay();
    return;
  }

  VGMColl *coll = qtVGMRoot.vgmColls()[list[0].row()];
  PlayerService::getInstance().playCollection(coll);
}

void VGMCollListView::handleStopRequest() {
  PlayerService::getInstance()->stop();
}

void VGMCollListView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void VGMCollListView::onVGMFileSelected(VGMFile* file, const QWidget* caller) {
  if (caller == this) {
    return;
  }

  auto *seq = dynamic_cast<VGMSeq *>(file);
  if (!seq || seq->assocColls.empty()) {
    return;
  }

  VGMColl *coll = seq->assocColls.front();
  const auto& colls = qtVGMRoot.vgmColls();
  auto it = std::find(colls.begin(), colls.end(), coll);
  if (it == colls.end()) {
    return;
  }

  const int row = static_cast<int>(std::distance(colls.begin(), it));
  const auto index = model()->index(row, 0);
  if (!index.isValid()) {
    return;
  }

  blockSignals(true);
  selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
  blockSignals(false);
  scrollTo(index, QAbstractItemView::EnsureVisible);
}

void VGMCollListView::updateContextualMenus() const {
  if (!selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMColls({});
    return;
  }

  QModelIndexList list = selectionModel()->selectedRows();
  QList<VGMColl*> colls;
  colls.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      colls.append(qtVGMRoot.vgmColls()[index.row()]);
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMColls(colls);
}
