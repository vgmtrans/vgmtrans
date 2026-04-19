/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractListModel>
#include <QListView>
#include <QKeyEvent>
#include <QString>
#include "widgets/FixedHeightListDelegate.h"

class VGMColl;
class VGMFile;
class VGMCollListView;
class QItemSelection;
class QResizeEvent;
class QWidget;
class EmptyStateWidget;

class VGMCollListViewModel : public QAbstractListModel {
public:
  explicit VGMCollListViewModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
  size_t collsBeforeLoad;
  bool isLoadingRawFile = false;
};

class VGMCollNameEditor : public FixedHeightListDelegate {
public:
  explicit VGMCollNameEditor(int itemHeight, QObject* parent = nullptr)
      : FixedHeightListDelegate(itemHeight, parent) {}

protected:
  void setEditorData(QWidget *editor, const QModelIndex &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model,
                    const QModelIndex &index) const override;
};

class VGMCollListView : public QListView {
  Q_OBJECT
public:
  explicit VGMCollListView(QWidget *parent = nullptr);
  [[nodiscard]] int visibleCollectionCount() const;
  void setFilterText(const QString& text);
  [[nodiscard]] QString filterText() const { return m_filterText; }
  void clearFilter();

signals:
  void nothingToPlay();
  void filterVisibilityChanged(int visibleCount, bool hasFilter);

public slots:
  void handlePlaybackRequest();
  static void handleStopRequest();

private:
  void resizeEvent(QResizeEvent *event) override;
  void applyFilter();
  void updateSearchEmptyState(int visibleCount, bool hasFilter);
  [[nodiscard]] bool matchesFilter(const VGMColl* coll) const;
  [[nodiscard]] bool indexIsVisible(const QModelIndex& index) const;
  void collectionMenu(const QPoint &pos) const;
  void keyPressEvent(QKeyEvent *e) override;
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void onVGMFileSelected(VGMFile* file, const QWidget* caller);
  void updateSelectedCollection() const;
  void updateContextualMenus() const;

  QString m_filterText;
  EmptyStateWidget *m_searchEmptyState = nullptr;
};
