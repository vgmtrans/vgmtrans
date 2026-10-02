/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "CollectionListView.h"

#include "models/ValueModels.h"
#include "widgets/EmptyStateWidget.h"
#include "widgets/FixedHeightListDelegate.h"
#include "widgets/ItemViewDensity.h"

#include <algorithm>

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QStyle>

namespace {
constexpr int kSearchEmptyCompactHeightThreshold = 170;

InstructionHint searchEmptyStateHeadingHint() {
  InstructionHint hint;
  hint.iconPath = QStringLiteral(":/icons/magnify.svg");
  hint.text = QStringLiteral("No matching collections");
  hint.fontScale = 1.30;
  hint.iconScale = 2.0;
  hint.fontWeight = QFont::DemiBold;
  hint.minPointSize = 12;
  return hint;
}
}  // namespace

CollectionListView::CollectionListView(QWidget* parent) : QListView(parent) {
  setAttribute(Qt::WA_MacShowFocusRect, false);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setResizeMode(QListView::Adjust);
  setIconSize(QSize(16, 16));
  setItemDelegate(new FixedHeightListDelegate(
      ItemViewDensity::listItemHeight(this), this));
  ItemViewDensity::apply(this);
  setWrapping(true);
  setDragEnabled(false);
  setAcceptDrops(false);
  setDropIndicatorShown(false);
  setDragDropMode(QAbstractItemView::NoDragDrop);
  setDefaultDropAction(Qt::MoveAction);

#ifdef Q_OS_MAC
  // A wrapping QListView otherwise adds unwanted padding below the scrollbar.
  const int scrollBarThickness = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
  QMargins margins = viewportMargins();
  margins.setBottom(margins.bottom() - scrollBarThickness);
  setViewportMargins(margins);
#endif

  m_searchEmptyState =
      new EmptyStateWidget(searchEmptyStateHeadingHint(), nullptr, viewport());
  m_searchEmptyState->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_searchEmptyState->setFocusPolicy(Qt::NoFocus);
  m_searchEmptyState->setBodyText({});
  m_searchEmptyState->setCompactLayoutHeightThreshold(
      kSearchEmptyCompactHeightThreshold);
  m_searchEmptyState->setGeometry(searchEmptyStateRect());
  m_searchEmptyState->hide();
}

void CollectionListView::setModel(QAbstractItemModel* newModel) {
  if (model() != nullptr) {
    disconnect(model(), nullptr, this, nullptr);
  }
  QListView::setModel(newModel);
  if (newModel != nullptr) {
    connect(newModel, &QAbstractItemModel::modelReset, this,
            &CollectionListView::updateSearchEmptyState);
    connect(newModel, &QAbstractItemModel::rowsInserted, this,
            &CollectionListView::updateSearchEmptyState);
    connect(newModel, &QAbstractItemModel::rowsRemoved, this,
            &CollectionListView::updateSearchEmptyState);
    connect(newModel, &QAbstractItemModel::layoutChanged, this,
            &CollectionListView::updateSearchEmptyState);
  }
  updateSearchEmptyState();
}

void CollectionListView::setFilterText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (m_filterText == trimmed) {
    return;
  }
  m_filterText = trimmed;
  const QVariant currentId = currentIndex().data(vgmtrans::ui::IdRole);
  if (auto* filter = qobject_cast<QSortFilterProxyModel*>(model())) {
    filter->setFilterFixedString(m_filterText);
  }

  QModelIndex replacement;
  if (currentId.isValid() && model() != nullptr) {
    for (int row = 0; row < model()->rowCount(); ++row) {
      const QModelIndex index = model()->index(row, 0);
      if (index.data(vgmtrans::ui::IdRole) == currentId) {
        replacement = index;
        break;
      }
    }
  }
  if (!replacement.isValid() && selectionModel() != nullptr) {
    const QModelIndexList selected = selectionModel()->selectedRows();
    if (!selected.isEmpty()) {
      replacement = selected.front();
    } else if (model() != nullptr && model()->rowCount() > 0) {
      replacement = model()->index(0, 0);
    }
  }
  if (selectionModel() != nullptr && replacement != currentIndex()) {
    selectionModel()->setCurrentIndex(replacement, QItemSelectionModel::NoUpdate);
  }
  updateSearchEmptyState();
}

void CollectionListView::clearFilter() {
  setFilterText({});
}

void CollectionListView::setStitchDragDropEnabled(bool enabled) {
  if (dragEnabled() == enabled) {
    return;
  }
  setDragEnabled(enabled);
  setDragDropMode(enabled ? QAbstractItemView::DragOnly
                          : QAbstractItemView::NoDragDrop);
}

int CollectionListView::visibleCollectionCount() const {
  return model() != nullptr ? model()->rowCount() : 0;
}

void CollectionListView::resizeEvent(QResizeEvent* event) {
  QListView::resizeEvent(event);
  if (m_searchEmptyState != nullptr) {
    m_searchEmptyState->setGeometry(searchEmptyStateRect());
  }
}

void CollectionListView::updateSearchEmptyState() {
  if (m_searchEmptyState == nullptr) {
    return;
  }
  const int visibleCount = visibleCollectionCount();
  const bool hasFilter = !m_filterText.isEmpty();
  const bool show = hasFilter && visibleCount == 0;
  m_searchEmptyState->setEmptyStateShown(show);
  m_searchEmptyState->setVisible(show);
  if (show) {
    m_searchEmptyState->setGeometry(searchEmptyStateRect());
    m_searchEmptyState->raise();
  }
  emit filterVisibilityChanged(visibleCount, hasFilter);
}

QRect CollectionListView::searchEmptyStateRect() const {
  QRect rect = viewport()->rect();
#ifdef Q_OS_MAC
  if (isWrapping()) {
    rect.setHeight(std::max(
        0, rect.height() - style()->pixelMetric(QStyle::PM_ScrollBarExtent)));
  }
#endif
  return rect;
}
