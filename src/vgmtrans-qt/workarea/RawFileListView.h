/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QAbstractTableModel>
#include <QTableView>

class RawFileListViewModel : public QAbstractTableModel {
    Q_OBJECT

   public:
    explicit RawFileListViewModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

   public slots:
    void AddRawFile();
    void RemoveRawFile();

   private:
    enum Property : uint8_t { Name = 0, ContainedFiles = 1 };
};

class RawFileListView : public QTableView {
    Q_OBJECT

   public:
    explicit RawFileListView(QWidget *parent = nullptr);

   private:
    void keyPressEvent(QKeyEvent *input) override;
    void RawFilesMenu(const QPoint &pos);
    void DeleteRawFiles();

    RawFileListViewModel *rawFileListViewModel;
    QMenu *rawfile_context_menu;
};
