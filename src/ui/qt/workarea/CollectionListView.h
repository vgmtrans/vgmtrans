/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QListView>

class EmptyStateWidget;

class CollectionListView final : public QListView {
  Q_OBJECT

public:
  explicit CollectionListView(QWidget* parent = nullptr);

  void setModel(QAbstractItemModel* model) override;
  void setFilterText(const QString& text);
  void clearFilter();
  void setStitchDragDropEnabled(bool enabled);
  [[nodiscard]] int visibleCollectionCount() const;

signals:
  void filterVisibilityChanged(int visibleCount, bool hasFilter);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void updateSearchEmptyState();
  [[nodiscard]] QRect searchEmptyStateRect() const;

  EmptyStateWidget* m_searchEmptyState{};
  QString m_filterText;
};
