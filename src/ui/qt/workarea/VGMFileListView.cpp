/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <algorithm>
#include <variant>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QSortFilterProxyModel>
#include <QSignalBlocker>
#include <QShortcut>
#include <QVBoxLayout>

#include "VGMFileListView.h"
#include "Helpers.h"
#include "QtVGMRoot.h"
#include "MdiArea.h"
#include "services/Settings.h"
#include "TableView.h"
#include "services/NotificationCenter.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMSeq.h"
#include "VGMMiscFile.h"
#include "VGMSampColl.h"
#include "services/MenuManager.h"

#include <QPushButton>

namespace {
QString typeLabelForFile(const VGMFile *file) {
  if (dynamic_cast<const VGMSeq *>(file)) {
    return "Seq";
  }
  if (dynamic_cast<const VGMInstrSet *>(file)) {
    return "InstrSet";
  }
  if (dynamic_cast<const VGMSampColl *>(file)) {
    return "SampColl";
  }
  return "Misc";
}

QString sortKeyForSort(VGMFile *file, VGMFileListModel::SortKey key) {
  if (!file) {
    return {};
  }
  switch (key) {
    case VGMFileListModel::SortKey::Name:
      return QString::fromStdString(file->name()).toLower();
    case VGMFileListModel::SortKey::Format:
      return QString::fromStdString(file->formatName()).toLower();
    case VGMFileListModel::SortKey::Type:
      return typeLabelForFile(file).toLower();
    case VGMFileListModel::SortKey::Added:
      return {};
  }
  return {};
}

class VGMFileListSortProxy final : public QSortFilterProxyModel {
public:
  explicit VGMFileListSortProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

  void setSort(VGMFileListModel::SortKey key, Qt::SortOrder order) {
    sortKey = key;
    sortOrder = order;
    invalidate();
    QSortFilterProxyModel::sort(0, Qt::AscendingOrder);
  }

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
    auto *model = dynamic_cast<VGMFileListModel *>(sourceModel());
    if (!model) {
      return QSortFilterProxyModel::lessThan(left, right);
    }

    VGMFile *leftFile = model->fileFromIndex(left);
    VGMFile *rightFile = model->fileFromIndex(right);
    if (!leftFile || !rightFile) {
      return left.row() < right.row();
    }

    if (sortKey == VGMFileListModel::SortKey::Added) {
      if (sortOrder == Qt::AscendingOrder) {
        return left.row() < right.row();
      }
      return left.row() > right.row();
    }

    const QString leftKey = sortKeyForSort(leftFile, sortKey);
    const QString rightKey = sortKeyForSort(rightFile, sortKey);
    if (leftKey == rightKey) {
      return left.row() < right.row();
    }
    if (sortOrder == Qt::AscendingOrder) {
      return leftKey < rightKey;
    }
    return leftKey > rightKey;
  }

private:
  VGMFileListModel::SortKey sortKey = VGMFileListModel::SortKey::Added;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
};
}

/*
 *  VGMFileListModel
 */

VGMFileListModel::VGMFileListModel(QObject *parent) : QAbstractTableModel(parent) {
  auto startResettingModel = [this]() { beginResetModel(); };
  auto endResettingModel = [this]() {
    endResetModel();
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
  };

  auto beginLoad = [this]() {
    filesBeforeLoad = pRoot->vgmFiles().size();
  };
  auto endLoad = [this]() {
    int filesLoaded = pRoot->vgmFiles().size() - filesBeforeLoad;
    if (filesLoaded <= 0)
      return;
    beginInsertRows(QModelIndex(), filesBeforeLoad, filesBeforeLoad + filesLoaded - 1);
    endInsertRows();
  };

  connect(&qtVGMRoot, &QtVGMRoot::UI_beginLoadRawFile, beginLoad);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endLoadRawFile, endLoad);
  connect(&qtVGMRoot, &QtVGMRoot::UI_beginRemoveVGMFiles, startResettingModel);
  connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveVGMFiles, endResettingModel);
}

QVariant VGMFileListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  VGMFileVariant vgmfilevariant = qtVGMRoot.vgmFiles().at(index.row());
  VGMFile *vgmfile = variantToVGMFile(vgmfilevariant);

  switch (index.column()) {
    case Property::Name: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->name());
      } else if (role == Qt::DecorationRole) {
        return iconForFile(vgmfilevariant);
      }
      break;
    }

    case Property::Format: {
      if (role == Qt::DisplayRole) {
        return QString::fromStdString(vgmfile->formatName());
      }
      break;
    }

    default:
      L_WARN("requested data for unexpected column index: {}", index.column());
      break;
  }

  return {};
}

QVariant VGMFileListModel::headerData(int column, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical || role != Qt::DisplayRole) {
    return {};
  }

  switch (column) {
    case Property::Name: {
      return "Name";
    }

    case Property::Format: {
      return "Format";
    }
    default: {
      return {};
    }
  }
}

int VGMFileListModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(qtVGMRoot.vgmFiles().size());
}

int VGMFileListModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return 2;
}

Qt::ItemFlags VGMFileListModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::ItemIsEnabled;
  }

  return QAbstractTableModel::flags(index);
}

VGMFile *VGMFileListModel::fileFromIndex(const QModelIndex &index) const {
  if (!index.isValid()) {
    return nullptr;
  }
  return variantToVGMFile(qtVGMRoot.vgmFiles()[index.row()]);
}

QModelIndex VGMFileListModel::indexFromFile(const VGMFile *file) const {
  if (!file) {
    return {};
  }

  auto it = std::find_if(qtVGMRoot.vgmFiles().begin(), qtVGMRoot.vgmFiles().end(),
                         [file](const VGMFileVariant &var) {
                           bool matches = false;
                           std::visit([&](auto &&arg) { matches = (file == arg); }, var);
                           return matches;
                         });
  if (it == qtVGMRoot.vgmFiles().end()) {
    return {};
  }
  int row = static_cast<int>(std::distance(qtVGMRoot.vgmFiles().begin(), it));
  return createIndex(row, 0);
}

/*
 *  VGMFileListView
 */

VGMFileListView::VGMFileListView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(1);

  auto *header = new QWidget(this);
  auto *header_layout = new QHBoxLayout();
  header_layout->setContentsMargins(7, 1, 7, 0);
  header_layout->setSpacing(4);

  QFont compactFont = font();
  if (compactFont.pointSizeF() > 0) {
    compactFont.setPointSizeF(std::max(8.0, compactFont.pointSizeF() - 2.0));
  } else if (compactFont.pixelSize() > 0) {
    compactFont.setPixelSize(std::max(10, compactFont.pixelSize() - 2));
  }

  const int controlHeight = 18;

  auto *sort_label = new QLabel("Sort by:");
  sort_label->setFont(compactFont);
  m_sortCombo = new QComboBox();
  m_sortCombo->setFont(compactFont);
  const QStringList sortOptions{"Added", "Type", "Format", "Name"};
  m_sortCombo->addItems(sortOptions);
  m_sortCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_sortCombo->setFixedHeight(controlHeight);
  m_sortCombo->setStyleSheet(
      "QComboBox { padding: 0px 6px 0px 6px; border-top-right-radius: 0px; "
      "border-bottom-right-radius: 0px; }"
      "QComboBox::drop-down { width: 0px; border: 0px; }"
      "QComboBox::down-arrow { image: none; width: 0px; height: 0px; }");
  {
    QFontMetrics fm(compactFont);
    int maxTextWidth = 0;
    for (const auto &text : sortOptions) {
      maxTextWidth = std::max(maxTextWidth, fm.horizontalAdvance(text));
    }
    const int popupWidth = maxTextWidth + 24;
    m_sortCombo->view()->setMinimumWidth(popupWidth);
  }

  m_sortOrderButton = new QPushButton();
  m_sortOrderButton->setFont(compactFont);
  m_sortOrderButton->setFixedSize(controlHeight, controlHeight);
  m_sortOrderButton->setAutoDefault(false);
  m_sortOrderButton->setFocusPolicy(Qt::NoFocus);
  m_sortOrderButton->setStyleSheet(
      "QPushButton { padding: 0px; border-top-left-radius: 0px; "
      "border-bottom-left-radius: 0px; }");
  updateSortButtonIcon();

  auto *sort_controls = new QWidget(header);
  auto *sort_controls_layout = new QHBoxLayout();
  sort_controls_layout->setContentsMargins(0, 0, 0, 0);
  sort_controls_layout->setSpacing(0);
  sort_controls_layout->addWidget(m_sortCombo);
  sort_controls_layout->addWidget(m_sortOrderButton);
  sort_controls->setLayout(sort_controls_layout);

  header_layout->addWidget(sort_label);
  header_layout->addWidget(sort_controls);
  header_layout->addStretch(1);
  header->setLayout(header_layout);
  header->setFixedHeight(controlHeight + 2);
  layout->addWidget(header);

  m_table = new TableView(this);
  m_source_model = new VGMFileListModel(m_table);
  m_sort_proxy = new VGMFileListSortProxy(m_table);
  m_sort_proxy->setSourceModel(m_source_model);
  m_table->setModel(m_sort_proxy);
  m_table->setSortingEnabled(false);
  m_table->horizontalHeader()->setSortIndicatorShown(false);
  m_table->horizontalHeader()->setSectionsClickable(false);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setContextMenuPolicy(Qt::CustomContextMenu);

  layout->addWidget(m_table, 1);
  setLayout(layout);

  connect(m_table, &QAbstractItemView::customContextMenuRequested, this, &VGMFileListView::itemMenu);
  connect(m_table, &QAbstractItemView::doubleClicked, this, &VGMFileListView::requestVGMFileView);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &VGMFileListView::onVGMFileSelected);
  connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &VGMFileListView::onSelectionChanged);
  connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, &VGMFileListView::handleCurrentChanged);

  connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &VGMFileListView::applySort);
  connect(m_sortOrderButton, &QPushButton::clicked, this, [this]() {
    m_sortOrder = (m_sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    updateSortButtonIcon();
    applySort();
  });

  auto *returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), m_table);
  returnShortcut->setContext(Qt::WidgetShortcut);
  connect(returnShortcut, &QShortcut::activated, this, [this]() {
    const auto current = m_table->currentIndex();
    if (current.isValid()) {
      requestVGMFileView(current);
    }
  });
  auto *enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), m_table);
  enterShortcut->setContext(Qt::WidgetShortcut);
  connect(enterShortcut, &QShortcut::activated, this, [this]() {
    const auto current = m_table->currentIndex();
    if (current.isValid()) {
      requestVGMFileView(current);
    }
  });

  m_table->installEventFilter(this);

  int storedOrder = Settings::the()->VGMFileListView.sortOrder();
  if (storedOrder != Qt::AscendingOrder && storedOrder != Qt::DescendingOrder) {
    storedOrder = Qt::DescendingOrder;
  }
  m_sortOrder = static_cast<Qt::SortOrder>(storedOrder);
  updateSortButtonIcon();

  int storedKey = Settings::the()->VGMFileListView.sortKey();
  if (storedKey < 0 || storedKey >= m_sortCombo->count()) {
    storedKey = static_cast<int>(VGMFileListModel::SortKey::Type);
  }
  {
    const QSignalBlocker blocker(m_sortCombo);
    m_sortCombo->setCurrentIndex(storedKey);
  }
  applySort();
}

void VGMFileListView::itemMenu(const QPoint &pos) {
  if (!m_table->selectionModel()->hasSelection()) {
    return;
  }

  QModelIndexList list = m_table->selectionModel()->selectedRows();

  auto selectedFiles = std::make_shared<std::vector<VGMFile*>>();
  selectedFiles->reserve(list.size());
  for (const auto &index : list) {
    if (index.isValid()) {
      const auto sourceIndex = m_sort_proxy->mapToSource(index);
      if (auto *file = m_source_model->fileFromIndex(sourceIndex)) {
        selectedFiles->push_back(file);
      }
    }
  }
  if (selectedFiles->empty()) {
    return;
  }
  auto menu = MenuManager::the()->createMenuForItems<VGMItem>(selectedFiles);
  menu->exec(m_table->mapToGlobal(pos));
  menu->deleteLater();
}

void VGMFileListView::onSelectionChanged(const QItemSelection&, const QItemSelection&) {
  updateContextualMenus();
}

void VGMFileListView::updateContextualMenus() const {
  if (!m_table->selectionModel()) {
    NotificationCenter::the()->updateContextualMenusForVGMFiles({});
    return;
  }

  QModelIndexList list = m_table->selectionModel()->selectedRows();
  QList<VGMFile*> files;
  files.reserve(list.size());

  for (const auto& index : list) {
    if (index.isValid()) {
      const auto sourceIndex = m_sort_proxy->mapToSource(index);
      if (auto *file = m_source_model->fileFromIndex(sourceIndex)) {
        files.append(file);
      }
    }
  }

  NotificationCenter::the()->updateContextualMenusForVGMFiles(files);
}

void VGMFileListView::requestVGMFileView(const QModelIndex& index) {
  if (auto *file = m_source_model->fileFromIndex(m_sort_proxy->mapToSource(index))) {
    MdiArea::the()->newView(file);
  }
}

// Update the status bar for the current selection
void VGMFileListView::updateStatusBar() const {
  if (!m_table->currentIndex().isValid()) {
    NotificationCenter::the()->updateStatusForItem(nullptr);
    return;
  }
  VGMFile* file = m_source_model->fileFromIndex(m_sort_proxy->mapToSource(m_table->currentIndex()));
  NotificationCenter::the()->updateStatusForItem(file);
}

void VGMFileListView::handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
  Q_UNUSED(previous);
  if (!current.isValid()) {
    NotificationCenter::the()->selectVGMFile(nullptr, this);
    return;
  }

  VGMFile *file = m_source_model->fileFromIndex(m_sort_proxy->mapToSource(current));
  NotificationCenter::the()->selectVGMFile(file, this);

  if (m_table->hasFocus())
    updateStatusBar();
}

void VGMFileListView::onVGMFileSelected(VGMFile* file, const QWidget* caller) {
  if (caller == this)
    return;

  if (file == nullptr) {
    m_table->clearSelection();
    return;
  }

  QModelIndex firstIndex = m_source_model->indexFromFile(file);
  if (!firstIndex.isValid()) {
    return;
  }

  // Select the row corresponding to the file
  const QModelIndex proxyFirst = m_sort_proxy->mapFromSource(firstIndex);
  if (!proxyFirst.isValid()) {
    return;
  }
  QModelIndex lastIndex =
      m_table->model()->index(proxyFirst.row(), m_table->model()->columnCount() - 1); // Last column of the row

  if (proxyFirst == m_table->currentIndex())
    return;

  QItemSelection selection(proxyFirst, lastIndex);
  m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  // Prevent the currentChanged callback from triggering so that we don't recursively
  // call NotificationCenter::selectVGMFile()
  m_table->selectionModel()->blockSignals(true);
  m_table->setCurrentIndex(proxyFirst);
  m_table->selectionModel()->blockSignals(false);

  m_table->scrollTo(proxyFirst, QAbstractItemView::EnsureVisible);
}

bool VGMFileListView::eventFilter(QObject *obj, QEvent *event) {
  if (obj == m_table && event->type() == QEvent::FocusIn) {
    updateStatusBar();
  }
  return QWidget::eventFilter(obj, event);
}

void VGMFileListView::applySort() {
  VGMFileListModel::SortKey key = VGMFileListModel::SortKey::Added;
  switch (m_sortCombo->currentIndex()) {
    case 1:
      key = VGMFileListModel::SortKey::Type;
      break;
    case 2:
      key = VGMFileListModel::SortKey::Format;
      break;
    case 3:
      key = VGMFileListModel::SortKey::Name;
      break;
    default:
      key = VGMFileListModel::SortKey::Added;
      break;
  }
  if (auto *proxy = dynamic_cast<VGMFileListSortProxy *>(m_sort_proxy)) {
    proxy->setSort(key, m_sortOrder);
  }
  Settings::the()->VGMFileListView.setSortKey(static_cast<int>(key));
  Settings::the()->VGMFileListView.setSortOrder(static_cast<int>(m_sortOrder));
}

void VGMFileListView::updateSortButtonIcon() {
  const int iconSize = 10;
  QPixmap pixmap(iconSize, iconSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor arrowColor = m_sortCombo->palette().color(QPalette::Text);
  painter.setPen(Qt::NoPen);
  painter.setBrush(arrowColor);

  QPolygon triangle;
  if (m_sortOrder == Qt::AscendingOrder) {
    triangle << QPoint(iconSize / 2, 1) << QPoint(iconSize - 1, iconSize - 2)
             << QPoint(1, iconSize - 2);
  } else {
    triangle << QPoint(1, 1) << QPoint(iconSize - 1, 1)
             << QPoint(iconSize / 2, iconSize - 2);
  }
  painter.drawPolygon(triangle);
  painter.end();

  m_sortOrderButton->setIcon(QIcon(pixmap));
  m_sortOrderButton->setIconSize(QSize(iconSize, iconSize));
}
