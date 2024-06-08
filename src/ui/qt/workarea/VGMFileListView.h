/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QAbstractTableModel>
#include <QKeyEvent>
#include <QSortFilterProxyModel>
#include "TableView.h"

#include "VGMFile.h"

class VGMFileListModel : public QAbstractTableModel {
    Q_OBJECT

  public:
    explicit VGMFileListModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;

  public slots:
    void addVGMFile();
    void removeVGMFile();

  private:
    enum Property : uint8_t { Name = 0, Format = 1 };
};

class VGMFileListView final : public TableView {
    Q_OBJECT

  public:
    explicit VGMFileListView(QWidget *parent = nullptr);

  public slots:
    static void requestVGMFileView(const QModelIndex& index);
    void removeVGMFile(const VGMFile *file) const;
    void onVGMFileSelected(VGMFile *file, const QWidget* caller);

  private:
    void updateStatusBar() const;
    void focusInEvent(QFocusEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
    void keyPressEvent(QKeyEvent *input) override;
    void itemMenu(const QPoint &pos);

    VGMFileListModel *view_model;
};