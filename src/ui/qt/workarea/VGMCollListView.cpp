/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollListView.h"

#include <QLineEdit>
#include <VGMColl.h>
#include <VGMExport.h>
#include "SequencePlayer.h"
#include "QtVGMRoot.h"
#include "services/MenuManager.h"

static const QIcon &VGMCollIcon() {
  static QIcon icon(":/images/collection.svg");
  return icon;
}

/*
 * VGMCollListViewModel
 */
VGMCollListViewModel::VGMCollListViewModel(QObject *parent) : QAbstractListModel(parent) {
  auto startResettingModel = [this]() {
    resettingModel = true;
    beginResetModel();
  };

  auto endResettingModel = [this]() {
    endResetModel();
    resettingModel = false;
  };

  connect(&qtVGMRoot, &QtVGMRoot::UI_BeganLoadingRawFile, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_EndedLoadingRawFile, endResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_BeganRemovingVGMFiles, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_EndedRemovingVGMFiles, endResettingModel);


  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMColl,
          [this]() {
            if (!resettingModel)
              dataChanged(index(0, 0), index(rowCount() - 1, 0));
          });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemoveVGMColl,
          [this]() {
            if (!resettingModel)
              dataChanged(index(0, 0), index(rowCount() - 1, 0));
          });
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
  auto menu = MenuManager::the()->CreateMenuForItems<VGMColl>(selectedColls);
  menu->exec(mapToGlobal(pos));
  menu->deleteLater();
}

void VGMCollListView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Space:
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
    nothingToPlay();
    return;
  }

  VGMColl *coll = qtVGMRoot.vgmColls()[list[0].row()];
  SequencePlayer::the().playCollection(coll);
}

void VGMCollListView::handleStopRequest() {
  SequencePlayer::the().stop();
}
