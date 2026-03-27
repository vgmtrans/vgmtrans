/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractListModel>
#include <QItemSelectionModel>
#include <QWidget>
#include "VGMColl.h"

class VGMFile;
class QLabel;
class QListView;

class VGMCollViewModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Role {
    IsCollectionRole = Qt::UserRole,
    IsLastFileRole,
  };

  explicit VGMCollViewModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  [[nodiscard]] VGMColl *coll() const { return m_coll; }
  [[nodiscard]] VGMFile *fileFromIndex(const QModelIndex& index) const;
  [[nodiscard]] QModelIndex indexFromFile(const VGMFile* file) const;
  [[nodiscard]] bool isCollectionIndex(const QModelIndex& index) const;
  [[nodiscard]] bool isLastFileIndex(const QModelIndex& index) const;
  bool containsVGMFile(const VGMFile* file) const;

public slots:
  void handleSelectedCollChanged(VGMColl* coll, QWidget* caller);
  void removeVGMColl(const VGMColl* coll);

public:
  VGMColl *m_coll;
};

class VGMCollView : public QWidget {
  Q_OBJECT
public:
  explicit VGMCollView(QWidget *parent = nullptr);

private:
  void keyPressEvent(QKeyEvent *e) override;
  void itemMenu(const QPoint &pos);
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void updateContextualMenus() const;

private slots:
  void removeVGMColl(const VGMColl *coll) const;
  void doubleClickedSlot(const QModelIndex&) const;
  void handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
  void onVGMFileSelected(const VGMFile *file, const QWidget* caller) const;

private:
  VGMCollViewModel *vgmCollViewModel;
  QListView *m_listview;
};
