#ifndef VGMTRANS_VGMFILEITEMMODEL_H
#define VGMTRANS_VGMFILEITEMMODEL_H

#include <QAbstractItemModel>

class VGMFile;

class VGMFileItemModel : public QAbstractItemModel {
    Q_OBJECT

public:
    VGMFileItemModel(VGMFile *file);

private:
    VGMFile* vgmfile;

public:
    virtual QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    virtual QModelIndex parent(const QModelIndex &child) const;
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const;
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
};

#endif //VGMTRANS_VGMFILEITEMMODEL_H
