//
// Created by Mike on 8/31/14.
//

#include <QKeyEvent>
#include "VGMFileListView.h"
#include "QtVGMRoot.h"
#include "VGMFile.h"
#include "VGMItem.h"

// ********************
// VGMFileListViewModel
// ********************

VGMFileListViewModel::VGMFileListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedVGMFile()), this, SLOT(changedVGMFiles()));
    QObject::connect(&qtVGMRoot, SIGNAL(UI_RemovedVGMFile()), this, SLOT(changedVGMFiles()));
}

int VGMFileListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vVGMFile.size();
}

QVariant VGMFileListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*qtVGMRoot.vVGMFile[index.row()]->GetName());
    }
    else if (role == Qt::DecorationRole) {
        FileType filetype = qtVGMRoot.vVGMFile[index.row()]->GetFileType();
        switch (filetype) {
            case FILETYPE_SEQ:
                return QIcon(":/images/music_transcripts-32.png");
            case FILETYPE_INSTRSET:
                return QIcon(":/images/piano-32.png");
            case FILETYPE_SAMPCOLL:
                return QIcon(":/images/audio_wave-32.png");
        }
        return QIcon(":/images/audio_file-32.png");
    }
    return QVariant();
}

void VGMFileListViewModel::changedVGMFiles()
{
    emit dataChanged(index(0, 0), index(0, 0));
}


// ***************
// VGMFileListView
// ***************

VGMFileListView::VGMFileListView(QWidget *parent)
        : QListView(parent)
{
    VGMFileListViewModel *vgmFileListViewModel = new VGMFileListViewModel(this);
    this->setModel(vgmFileListViewModel);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setSelectionRectVisible(true);
}

void VGMFileListView::keyPressEvent(QKeyEvent* e)
{
    // On Backspace or Delete keypress, remove all selected files
    if( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
    {
        QModelIndexList list = this->selectionModel()->selectedIndexes();

        if (list.isEmpty())
            return;

        QList<VGMFile*> filesToClose;
        foreach(const QModelIndex &index, list) {
            if (index.row() < qtVGMRoot.vVGMFile.size())
                filesToClose.append(qtVGMRoot.vVGMFile[index.row()]);
        }

        foreach(VGMFile *file, filesToClose) {
            qtVGMRoot.RemoveVGMFile(file);
        }
    }
}