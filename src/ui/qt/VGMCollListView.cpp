//
// Created by Mike on 8/31/14.
//

#include <qdebug.h>
#include <qevent.h>
#include "SF2File.h"
#include "VGMSeq.h"
#include "VGMCollListView.h"
#include "QtVGMRoot.h"
#include "VGMColl.h"

const int cellWidth = 200;
const int cellHeight = 20;

// ********************
// VGMCollListViewModel
// ********************

VGMCollListViewModel::VGMCollListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    QObject::connect(&qtVGMRoot, SIGNAL(UI_AddedVGMColl()), this, SLOT(changedVGMColls()));
    QObject::connect(&qtVGMRoot, SIGNAL(UI_RemovedVGMColl()), this, SLOT(changedVGMColls()));
}

int VGMCollListViewModel::rowCount ( const QModelIndex & parent) const
{
    return qtVGMRoot.vVGMColl.size();
}

QVariant VGMCollListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*qtVGMRoot.vVGMColl[index.row()]->GetName());
    }
    else if (role == Qt::DecorationRole) {
        return QIcon(":/images/music_folder-32.png");
    }
    return QVariant();
}

void VGMCollListViewModel::changedVGMColls()
{
    emit dataChanged(index(0, 0), index(0, 0));
}


// ***************
// VGMCollListView
// ***************

VGMCollListView::VGMCollListView(QWidget *parent)
        : QListView(parent)
{
    VGMCollListViewModel *vgmCollListViewModel = new VGMCollListViewModel(this);
    this->setModel(vgmCollListViewModel);
    this->setSelectionMode(QAbstractItemView::SingleSelection);
    this->setGridSize(QSize(cellWidth, cellHeight));
    this->setWrapping(true);
//    this->setViewMode(QListView::IconMode);
//    this->setFlow(QListView::LeftToRight);
//    this->setSelectionRectVisible(true);
}

void VGMCollListView::keyPressEvent(QKeyEvent* e)
{
    // On a spacebar key press, play the selected collection
    if( e->key() == Qt::Key_Space)
    {
        QModelIndexList list = this->selectionModel()->selectedIndexes();
        if (list.size() == 0 || list[0].row() >= qtVGMRoot.vVGMColl.size())
            return;

        VGMColl* coll = qtVGMRoot.vVGMColl[list[0].row()];
        VGMSeq* seq = coll->GetSeq();
        SF2File* sf2 = coll->CreateSF2File();
        MidiFile* midi = seq->ConvertToMidi();

        std::vector<uint8_t> midiBuf;
        midi->WriteMidiToBuffer(midiBuf);

        const void* rawSF2 = sf2->SaveToMem();

        
    }
}