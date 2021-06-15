/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTableView>
#include <QAbstractTableModel>
#include <QKeyEvent>
#include <QSortFilterProxyModel>

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
    void AddVGMFile();
    void RemoveVGMFile();

   private:
    enum Property : uint8_t { Name = 0, Type = 1, Format = 2 };
};

class VGMFileListView final : public QTableView {
    Q_OBJECT

   public:
    explicit VGMFileListView(QWidget *parent = nullptr);

   public slots:
    void RequestVGMFileView(QModelIndex index);
    void RemoveVGMFile(VGMFile *file);

   private:
    void keyPressEvent(QKeyEvent *input) override;
    void ItemMenu(const QPoint &pos);

    VGMFileListModel *view_model;
};