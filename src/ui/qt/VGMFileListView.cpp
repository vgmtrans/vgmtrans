//
// Created by Mike on 8/31/14.
//

#include <QKeyEvent>
#include <QDebug>
#include "VGMFileListView.h"
#include "QtVGMRoot.h"
#include "VGMFile.h"
#include "VGMItem.h"
#include "MdiArea.h"
#include "VGMFileView.h"
#include "Helpers.h"

// ********************
// VGMFileListViewModel
// ********************

VGMFileListViewModel::VGMFileListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    connect(&qtVGMRoot, SIGNAL(UI_AddedVGMFile()), this, SLOT(changedVGMFiles()));
    connect(&qtVGMRoot, SIGNAL(UI_RemovedVGMFile()), this, SLOT(changedVGMFiles()));
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
        return iconForFileType(filetype);
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


//    connect(this, SIGNAL(clicked(QModelIndex)),this,SLOT(myItemSelected(QModelIndex)));
    connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(doubleClickedSlot(QModelIndex)));
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

void VGMFileListView::doubleClickedSlot(QModelIndex index)
{
    VGMFile *vgmFile = qtVGMRoot.vVGMFile[index.row()];
    VGMFileView *vgmFileView = new VGMFileView(vgmFile);
    QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());
    vgmFileView->setWindowTitle(vgmFileName);
    vgmFileView->setWindowIcon(iconForFileType(vgmFile->GetFileType()));

    MdiArea::getInstance()->addSubWindow(vgmFileView);

    vgmFileView->show();
}