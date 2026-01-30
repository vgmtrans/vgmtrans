/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractListModel>
#include <QWidget>

class VGMColl;
class VGMFile;
class VGMSeq;
class QLabel;
class QListView;
class QPushButton;
class QItemSelection;

class PropertyViewModel : public QAbstractListModel {
  Q_OBJECT
public:
  explicit PropertyViewModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  void setSequence(VGMSeq *seq);
  [[nodiscard]] VGMFile *fileFromIndex(const QModelIndex &index) const;
  [[nodiscard]] QModelIndex indexFromFile(const VGMFile *file) const;
  [[nodiscard]] bool containsVGMFile(const VGMFile *file) const;
  [[nodiscard]] VGMSeq *sequence() const { return m_seq; }
  [[nodiscard]] VGMColl *collection() const { return m_coll; }

private:
  VGMSeq *m_seq{};
  VGMColl *m_coll{};
};

class PropertyView final : public QWidget {
  Q_OBJECT
public:
  explicit PropertyView(QWidget *parent = nullptr);

signals:
  void nothingToPlay();

public slots:
  void handlePlaybackRequest();
  void handleStopRequest();

private:
  void keyPressEvent(QKeyEvent *e) override;
  void itemMenu(const QPoint &pos);
  void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
  void onVGMFileSelected(VGMFile *file, const QWidget *caller);
  void handleCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
  void updateContextualMenus() const;
  void updateHeader();
  void setSelectedSequence(VGMSeq *seq);
  void doubleClickedSlot(const QModelIndex &index) const;

  PropertyViewModel *m_model{};
  QListView *m_listview{};
  QLabel *m_title{};
  QPushButton *m_edit_button{};
  VGMSeq *m_current_seq{};
  bool m_ignore_next_non_seq_selection = false;
};
