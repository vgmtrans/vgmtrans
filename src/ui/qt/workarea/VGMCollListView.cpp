/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollListView.h"

#include <algorithm>
#include <QStringView>
#include <QLineEdit>
#include <QResizeEvent>
#include <VGMColl.h>
#include <VGMExport.h>
#include <VGMSeq.h>
#include "SequencePlayer.h"
#include "widgets/EmptyStateWidget.h"
#include "widgets/ItemViewDensity.h"
#include "workarea/MdiArea.h"
#include "QtVGMRoot.h"
#include "services/MenuManager.h"
#include "services/NotificationCenter.h"
#include "util/UIHelpers.h"

static constexpr int kSearchEmptyCompactHeightThreshold = 170;

static InstructionHint SearchEmptyStateHeadingHint() {
  InstructionHint hint;
  hint.iconPath = QStringLiteral(":/icons/magnify.svg");
  hint.text = QStringLiteral("No matching collections");
  hint.fontScale = 1.30;
  hint.iconScale = 2.0;
  hint.fontWeight = QFont::DemiBold;
  hint.minPointSize = 12;
  return hint;
}

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

  setContextMenuPolicy(Qt::CustomContextMenu);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setResizeMode(QListView::Adjust);
  setIconSize(QSize(16, 16));
  setItemDelegate(new VGMCollNameEditor(ItemViewDensity::listItemHeight(this), this));
  ItemViewDensity::apply(this);
  setWrapping(true);

#ifdef Q_OS_MAC
  // On MacOS, a wrapping QListView gives unwanted padding to the scrollbar. This compensates.
  if (isWrapping()) {
    int scrollBarThickness = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    QMargins margins = viewportMargins();
    margins.setBottom(margins.bottom() - scrollBarThickness);
    setViewportMargins(margins);
  }
#endif

  m_searchEmptyState = new EmptyStateWidget(SearchEmptyStateHeadingHint(), nullptr, viewport());
  m_searchEmptyState->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_searchEmptyState->setFocusPolicy(Qt::NoFocus);
  m_searchEmptyState->setBodyText(QString());
  m_searchEmptyState->setCompactLayoutHeightThreshold(kSearchEmptyCompactHeightThreshold);
  m_searchEmptyState->setGeometry(viewport()->rect());
  m_searchEmptyState->hide();

  connect(this, &QListView::doubleClicked, this,
          &VGMCollListView::handlePlaybackRequest);
  connect(this, &QAbstractItemView::customContextMenuRequested, this,
          &VGMCollListView::collectionMenu);
  connect(model, &VGMCollListViewModel::dataChanged, [this]() {
    applyFilter();
    if (!selectedIndexes().empty()) {
      selectionModel()->currentChanged(selectedIndexes().front(), {});
    } else {
      selectionModel()->currentChanged({}, {});
    }
  });
  connect(model, &QAbstractItemModel::rowsInserted, this, [this]() { applyFilter(); });
  connect(model, &QAbstractItemModel::rowsRemoved, this, [this]() { applyFilter(); });
  connect(model, &QAbstractItemModel::modelReset, this, [this]() { applyFilter(); });
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMCollListView::onSelectionChanged);
  connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex&, const QModelIndex&) { updateSelectedCollection(); });
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this,
          &VGMCollListView::onVGMFileSelected);

  applyFilter();
}

int VGMCollListView::visibleCollectionCount() const {
  if (!model()) {
    return 0;
  }

  int visibleCount = 0;
  const int totalRows = model()->rowCount();
  for (int row = 0; row < totalRows; ++row) {
    if (!isRowHidden(row)) {
      ++visibleCount;
    }
  }
  return visibleCount;
}

void VGMCollListView::setFilterText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (m_filterText == trimmed) {
    return;
  }
  m_filterText = trimmed;
  applyFilter();
}

void VGMCollListView::clearFilter() {
  setFilterText(QString());
}

void VGMCollListView::applyFilter() {
  const auto& allColls = qtVGMRoot.vgmColls();
  int visibleCount = 0;
  const bool hasFilter = !m_filterText.isEmpty();
  bool currentHidden = false;
  QModelIndex firstVisibleIndex;
  const QModelIndex current = selectionModel()->currentIndex();

  for (int row = 0; row < static_cast<int>(allColls.size()); ++row) {
    const bool visible = matchesFilter(allColls[static_cast<size_t>(row)]);
    setRowHidden(row, !visible);
    if (visible) {
      ++visibleCount;
    }

    const QModelIndex index = model()->index(row, 0);
    if (visible && !firstVisibleIndex.isValid() && index.isValid()) {
      firstVisibleIndex = index;
    }
    if (index.isValid() && selectionModel()->isSelected(index) && !visible) {
      selectionModel()->select(index, QItemSelectionModel::Deselect);
      if (current == index) {
        currentHidden = true;
      }
    }
  }

  if (currentHidden) {
    QModelIndex replacementCurrent;
    const QModelIndexList selected = selectionModel()->selectedRows();
    if (!selected.isEmpty()) {
      replacementCurrent = selected.front();
    } else if (firstVisibleIndex.isValid()) {
      replacementCurrent = firstVisibleIndex;
    }
    selectionModel()->setCurrentIndex(replacementCurrent, QItemSelectionModel::NoUpdate);
  }

  updateContextualMenus();
  updateSelectedCollection();
  updateSearchEmptyState(visibleCount, hasFilter);
  emit filterVisibilityChanged(visibleCount, hasFilter);
}

void VGMCollListView::updateSearchEmptyState(int visibleCount, bool hasFilter) {
  if (!m_searchEmptyState) {
    return;
  }

  const bool show = hasFilter && visibleCount == 0;
  m_searchEmptyState->setEmptyStateShown(show);
  m_searchEmptyState->setVisible(show);
  if (show) {
    m_searchEmptyState->setGeometry(viewport()->rect());
    m_searchEmptyState->raise();
  }
}

void VGMCollListView::resizeEvent(QResizeEvent *event) {
  QListView::resizeEvent(event);
  if (m_searchEmptyState) {
    m_searchEmptyState->setGeometry(viewport()->rect());
  }
}

bool VGMCollListView::matchesFilter(const VGMColl* coll) const {
  if (!coll) {
    return false;
  }

  if (m_filterText.isEmpty()) {
    return true;
  }

  const QStringView needle = QStringView(m_filterText);
  const QString collName = QString::fromStdString(coll->name());
  if (collName.contains(needle, Qt::CaseInsensitive)) {
    return true;
  }

  if (const VGMSeq* seq = coll->seq()) {
    const QString seqName = QString::fromStdString(seq->name());
    if (seqName.contains(needle, Qt::CaseInsensitive)) {
      return true;
    }
  }

  return false;
}

bool VGMCollListView::indexIsVisible(const QModelIndex& index) const {
  return index.isValid() && !isRowHidden(index.row());
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
    if (indexIsVisible(index)) {
      selectedColls->push_back(qtVGMRoot.vgmColls()[index.row()]);
    }
  }
  if (selectedColls->empty()) {
    return;
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
  if (list.empty() || !indexIsVisible(list[0]) || list[0].row() >= model()->rowCount()) {
    if (SequencePlayer::the().activeCollection() != nullptr) {
      SequencePlayer::the().toggle();
      return;
    }
    nothingToPlay();
    return;
  }

  VGMColl *coll = qtVGMRoot.vgmColls()[list[0].row()];
  SequencePlayer::the().playCollection(coll);
}

void VGMCollListView::handleStopRequest() {
  SequencePlayer::the().stop();
}

void VGMCollListView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
  updateSelectedCollection();
}

void VGMCollListView::onVGMFileSelected(VGMFile* file, const QWidget* caller) {
  if (caller == this) {
    return;
  }

  if (file == nullptr) {
    clearSelection();
    updateSelectedCollection();
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
  if (!index.isValid() || !indexIsVisible(index)) {
    clearSelection();
    selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
    updateContextualMenus();
    updateSelectedCollection();
    return;
  }

  blockSignals(true);
  selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
  blockSignals(false);
  scrollTo(index, QAbstractItemView::EnsureVisible);
}

void VGMCollListView::updateSelectedCollection() const {
  if (!selectionModel() || !selectionModel()->hasSelection()) {
    NotificationCenter::the()->selectVGMColl(nullptr, const_cast<VGMCollListView*>(this));
    return;
  }

  const QModelIndexList selected = selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    NotificationCenter::the()->selectVGMColl(nullptr, const_cast<VGMCollListView*>(this));
    return;
  }

  QModelIndex index = selectionModel()->currentIndex();
  if (!index.isValid() || !selectionModel()->isSelected(index)) {
    index = selected.front();
  }

  if (!indexIsVisible(index) ||
      static_cast<size_t>(index.row()) >= qtVGMRoot.vgmColls().size()) {
    NotificationCenter::the()->selectVGMColl(nullptr, const_cast<VGMCollListView*>(this));
    return;
  }

  NotificationCenter::the()->selectVGMColl(qtVGMRoot.vgmColls()[index.row()],
                                           const_cast<VGMCollListView*>(this));
}

void VGMCollListView::updateContextualMenus() const {
  QModelIndexList list = selectionModel()->selectedRows();
  QList<VGMColl*> colls;
  colls.reserve(list.size());

  for (const auto& index : list) {
    if (indexIsVisible(index)) {
      colls.append(qtVGMRoot.vgmColls()[index.row()]);
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMColls(colls);
}
