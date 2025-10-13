/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractListModel>
#include <QGroupBox>
#include <QItemSelectionModel>
#include "VGMColl.h"

class VGMFile;
class QLabel;
class QListView;
class QLineEdit;

class VGMCollViewModel : public QAbstractListModel {
  Q_OBJECT
public:
  VGMCollViewModel(const QItemSelectionModel *collListSelModel, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  [[nodiscard]] VGMFile *fileFromIndex(const QModelIndex& index) const;
  [[nodiscard]] QModelIndex indexFromFile(const VGMFile* file) const;
  bool containsVGMFile(const VGMFile* file) const;

public slots:
  void handleNewCollSelected(const QModelIndex& modelIndex);
  void removeVGMColl(const VGMColl* coll);

public:
  VGMColl *m_coll;
};

class VGMCollView : public QGroupBox {
  Q_OBJECT
public:
  VGMCollView(QItemSelectionModel *collListSelModel, QWidget *parent = nullptr);

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
  QLineEdit *m_collection_title;
  QListView *m_listview;
  QLabel *m_title;
};