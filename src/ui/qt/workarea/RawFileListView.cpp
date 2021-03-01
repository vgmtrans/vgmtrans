//
// Created by Mike on 8/31/14.
//

#include <QKeyEvent>
#include <QStandardItem>
#include "RawFileListView.h"


// ********************
// RawFileListViewModel
// ********************

RawFileListViewModel::RawFileListViewModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedRawFile()), this, SLOT(changedRawFiles()));
    QObject::connect(&qtVGMRoot, SIGNAL(UI_RemovedRawFile()), this, SLOT(changedRawFiles()));
}

int RawFileListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vRawFile.size();
}

QVariant RawFileListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        const wchar_t *filename = qtVGMRoot.vRawFile[index.row()]->GetFileName();
        return QString::fromWCharArray(filename);
    }
    else if (role == Qt::DecorationRole) {
        return QIcon(":/images/file-32.png");
    }

    return QVariant();
}

QVariant RawFileListViewModel::headerData(int section, Qt::Orientation orientation, int role) const {
    return "Test Header";
}

void RawFileListViewModel::changedRawFiles()
{
    emit dataChanged(index(0, 0), index(0, 0));
}


// ***************
// RawFileListView
// ***************

RawFileListView::RawFileListView(QWidget *parent)
        : QListView(parent)
{
    RawFileListViewModel *rawFileListViewModel = new RawFileListViewModel(this);
    this->setModel(rawFileListViewModel);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setSelectionRectVisible(true);
}

void RawFileListView::keyPressEvent(QKeyEvent* e)
{
    // On Backspace or Delete keypress, remove all selected files
    if( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
    {
        QModelIndexList list = this->selectionModel()->selectedIndexes();

        if (list.isEmpty())
            return;

        QList<RawFile*> filesToClose;
        foreach(const QModelIndex &index, list) {
            if (index.row() < qtVGMRoot.vRawFile.size())
                filesToClose.append(qtVGMRoot.vRawFile[index.row()]);
        }


        foreach(RawFile *file, filesToClose) {
            printf("In Loop");
            qtVGMRoot.CloseRawFile(file);
        }
    }
}