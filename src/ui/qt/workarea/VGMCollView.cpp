/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMCollView.h"

#include <algorithm>
#include <QPainter>
#include <QVBoxLayout>
#include <QListView>
#include <QKeyEvent>
#include <QStyleOptionViewItem>

#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>
#include <VGMMiscFile.h>
#include <VGMColl.h>
#include "widgets/FixedHeightListDelegate.h"
#include "widgets/ItemViewDensity.h"
#include "QtVGMRoot.h"
#include "Helpers.h"
#include "MdiArea.h"
#include "services/NotificationCenter.h"
#include "services/MenuManager.h"

namespace {
const QIcon &VGMCollIcon() {
  static QIcon icon(":/icons/collection.svg");
  return icon;
}

class VGMCollTreeDelegate final : public FixedHeightListDelegate {
public:
  explicit VGMCollTreeDelegate(int itemHeight, QObject *parent = nullptr)
      : FixedHeightListDelegate(itemHeight, parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);

    if (index.data(VGMCollViewModel::IsCollectionRole).toBool()) {
      QStyledItemDelegate::paint(painter, itemOption, index);
      return;
    }

    const int indent = itemOption.decorationSize.width() + 12;
    const QRect rowRect = itemOption.rect;

    itemOption.rect.adjust(indent, 0, 0, 0);
    QStyledItemDelegate::paint(painter, itemOption, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    QPen pen(option.palette.color(QPalette::Text), 1);
    pen.setCosmetic(true);
    painter->setPen(pen);

    const int branchX = rowRect.left() + indent / 2;
    const int centerY = rowRect.center().y();
    const int fullBranchEndX = itemOption.rect.left() + itemOption.decorationSize.width() / 2;
    const int branchEndX = branchX + (fullBranchEndX - branchX) / 1.5;
    const int trunkEndY = index.data(VGMCollViewModel::IsLastFileRole).toBool() ? centerY : rowRect.bottom();

    painter->drawLine(branchX, rowRect.top(), branchX, trunkEndY);
    painter->drawLine(branchX, centerY, branchEndX, centerY);
    painter->restore();
  }
};
}

VGMCollViewModel::VGMCollViewModel(QObject *parent)
    : QAbstractListModel(parent), m_coll(nullptr) {
  connect(NotificationCenter::the(), &NotificationCenter::vgmCollSelected,
          this, &VGMCollViewModel::handleSelectedCollChanged);
}

int VGMCollViewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid() || m_coll == nullptr) {
    return 0;
  }
  // One parent collection row, then the collection contents including the sequence.
  return static_cast<int>(m_coll->instrSets().size() + m_coll->sampColls().size() +
                          m_coll->miscFiles().size() + 2);
}

QVariant VGMCollViewModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || !m_coll) {
    return {};
  }

  if (isCollectionIndex(index)) {
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
      return QString::fromStdString(m_coll->name());
    } else if (role == Qt::DecorationRole) {
      return VGMCollIcon();
    } else if (role == IsCollectionRole) {
      return true;
    } else if (role == IsLastFileRole) {
      return false;
    }
    return {};
  }

  auto file = fileFromIndex(index);
  if (!file) {
    return {};
  }

  if (role == Qt::DisplayRole) {
    return QString::fromStdString(file->name());
  } else if (role == Qt::DecorationRole) {
    return iconForFile(vgmFileToVariant(file));
  } else if (role == IsCollectionRole) {
    return false;
  } else if (role == IsLastFileRole) {
    return isLastFileIndex(index);
  }

  return QVariant();
}

void VGMCollViewModel::handleSelectedCollChanged(VGMColl* coll, QWidget* caller) {
  Q_UNUSED(caller);
  if (m_coll == coll) {
    return;
  }
  beginResetModel();
  m_coll = coll;
  endResetModel();
}

VGMFile *VGMCollViewModel::fileFromIndex(const QModelIndex& index) const {
  if (!m_coll || !index.isValid() || isCollectionIndex(index))
    return nullptr;

  size_t row = index.row() - 1;
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
  if (!m_coll) {
    return QModelIndex();
  }

  int row = 1;

  // Check in miscfiles
  auto miscIt = std::ranges::find(m_coll->miscFiles(), file);
  if (miscIt != m_coll->miscFiles().end()) {
    return createIndex(row + static_cast<int>(std::distance(m_coll->miscFiles().begin(), miscIt)), 0);
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

bool VGMCollViewModel::isCollectionIndex(const QModelIndex& index) const {
  return m_coll != nullptr && index.isValid() && index.row() == 0;
}

bool VGMCollViewModel::isLastFileIndex(const QModelIndex& index) const {
  return m_coll != nullptr && index.isValid() && index.row() == rowCount() - 1;
}

bool VGMCollViewModel::containsVGMFile(const VGMFile* file) const {
  if (!m_coll)
    return false;
  return m_coll->containsVGMFile(file);
}

void VGMCollViewModel::removeVGMColl(const VGMColl* coll) {
  if (m_coll != coll)
    return;

  handleSelectedCollChanged(nullptr, nullptr);
}


VGMCollView::VGMCollView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  m_listview = new QListView(this);
  m_listview->setAttribute(Qt::WA_MacShowFocusRect, false);
  m_listview->setIconSize(QSize(16, 16));
  m_listview->setItemDelegate(new VGMCollTreeDelegate(
      ItemViewDensity::listItemHeight(m_listview) + ItemViewDensity::listSpacing(m_listview), m_listview));
  m_listview->setContextMenuPolicy(Qt::CustomContextMenu);
  ItemViewDensity::apply(m_listview);
  m_listview->setSpacing(0);
  layout->addWidget(m_listview);

  vgmCollViewModel = new VGMCollViewModel(this);
  m_listview->setModel(vgmCollViewModel);
  m_listview->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_listview->setSelectionRectVisible(true);
  m_listview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, &VGMCollView::removeVGMColl);
  connect(m_listview, &QWidget::customContextMenuRequested, this, &VGMCollView::itemMenu);
  connect(m_listview, &QListView::doubleClicked, this, &VGMCollView::doubleClickedSlot);
  connect(m_listview->selectionModel(), &QItemSelectionModel::currentChanged, this, &VGMCollView::handleCurrentChanged);
  connect(m_listview->selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMCollView::onSelectionChanged);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &VGMCollView::onVGMFileSelected);

  setLayout(layout);
}

void VGMCollView::itemMenu(const QPoint &pos) {
  const auto selectionModel = m_listview->selectionModel();
  if (!selectionModel->hasSelection()) {
    return;
  }

  QModelIndexList list = selectionModel->selectedRows();

  auto selectedColls = std::make_shared<std::vector<VGMColl*>>();
  auto selectedFiles = std::make_shared<std::vector<VGMFile*>>();
  selectedColls->reserve(list.size());
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      if (vgmCollViewModel->isCollectionIndex(index)) {
        selectedColls->push_back(vgmCollViewModel->coll());
      } else if (auto *file = vgmCollViewModel->fileFromIndex(index)) {
        selectedFiles->push_back(file);
      }
    }
  }

  QMenu *menu = nullptr;
  if (!selectedFiles->empty()) {
    menu = MenuManager::the()->createMenuForItems<VGMItem>(selectedFiles);
  } else if (!selectedColls->empty()) {
    menu = MenuManager::the()->createMenuForItems<VGMColl>(selectedColls);
  }
  if (!menu) {
    return;
  }

  menu->exec(m_listview->viewport()->mapToGlobal(pos));
  menu->deleteLater();
}

void VGMCollView::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return: {
      QModelIndex currentIndex = m_listview->currentIndex();
      if (currentIndex.isValid()) {
        if (auto *file = vgmCollViewModel->fileFromIndex(currentIndex)) {
          MdiArea::the()->newView(file);
        }
      }
      break;
    }
    default:
      QWidget::keyPressEvent(e);
  }
}

void VGMCollView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void VGMCollView::updateContextualMenus() const {
  if (!m_listview->selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
    NotificationCenter::the()->updateContextualMenusForVGMColls({});
    return;
  }

  QModelIndexList list = m_listview->selectionModel()->selectedRows();
  QList<VGMFile*> files;
  QList<VGMColl*> colls;
  files.reserve(list.size());
  colls.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      if (vgmCollViewModel->isCollectionIndex(index)) {
        colls.append(vgmCollViewModel->coll());
      } else if (auto *file = vgmCollViewModel->fileFromIndex(index)) {
        files.append(file);
      }
    }
  }

  if (!files.isEmpty()) {
    NotificationCenter::the()->updateContextualMenusForVGMColls({});
    NotificationCenter::the()->updateContextualMenusForVGMFiles(files);
    return;
  }

  NotificationCenter::the()->updateContextualMenusForVGMFiles({});
  NotificationCenter::the()->updateContextualMenusForVGMColls(colls);
}

void VGMCollView::removeVGMColl(const VGMColl *coll) const {
  if (vgmCollViewModel->m_coll != coll)
    return;

  vgmCollViewModel->removeVGMColl(coll);
}

void VGMCollView::doubleClickedSlot(const QModelIndex& index) const {
  if (auto *fileToOpen = vgmCollViewModel->fileFromIndex(index)) {
    MdiArea::the()->newView(fileToOpen);
  }
}

void VGMCollView::handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
  Q_UNUSED(previous);

  if (!current.isValid()) {
    NotificationCenter::the()->selectVGMFile(nullptr, this);
    return;
  }

  if (vgmCollViewModel->isCollectionIndex(current)) {
    NotificationCenter::the()->selectVGMColl(vgmCollViewModel->coll(), this);
    if ((hasFocus() || m_listview->hasFocus()) && vgmCollViewModel->coll()) {
      const QString name = QString{"<b>%1</b>"}.arg(QString::fromStdString(vgmCollViewModel->coll()->name()));
      const QIcon &icon = VGMCollIcon();
      NotificationCenter::the()->updateStatus(name, "", &icon);
    }
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
