//
// Created by Mike on 8/31/14.
//

#include <qdebug.h>
#include <qevent.h>
#include "SF2File.h"
#include "VGMSeq.h"
#include "VGMCollListView.h"
#include "QtUICallbacks.h"
#include "main/Core.h"
#include "VGMColl.h"
#include "MusicPlayer.h"

const int cellWidth = 200;
const int cellHeight = 20;

// ********************
// VGMCollListViewModel
// ********************

VGMCollListViewModel::VGMCollListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
    QObject::connect(&qtUICallbacks, SIGNAL(UI_AddedVGMColl()), this, SLOT(changedVGMColls()));
    QObject::connect(&qtUICallbacks, SIGNAL(UI_RemovedVGMColl()), this, SLOT(changedVGMColls()));
}

int VGMCollListViewModel::rowCount ( const QModelIndex & parent) const
{
    return core.vVGMColl.size();
}

QVariant VGMCollListViewModel::data ( const QModelIndex & index, int role ) const
{
    if (role == Qt::DisplayRole) {
        return QString::fromStdWString(*core.vVGMColl[index.row()]->GetName());
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
    setAttribute(Qt::WA_MacShowFocusRect, 0);

    VGMCollListViewModel *vgmCollListViewModel = new VGMCollListViewModel(this);
    setModel(vgmCollListViewModel);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setGridSize(QSize(cellWidth, cellHeight));
    setWrapping(true);
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
        if (list.size() == 0 || list[0].row() >= core.vVGMColl.size())
            return;

        VGMColl* coll = core.vVGMColl[list[0].row()];
        VGMSeq* seq = coll->GetSeq();
        SF2File* sf2 = coll->CreateSF2File();
        MidiFile* midi = seq->ConvertToMidi();

        midi->SaveMidiFile(L"test.mid");

        std::vector<uint8_t> midiBuf;
        midi->WriteMidiToBuffer(midiBuf);

        const void* rawSF2 = sf2->SaveToMem();

        qDebug() << "Gonna play us some music";
        MusicPlayer& musicPlayer = MusicPlayer::getInstance();

        musicPlayer.Stop();
        musicPlayer.LoadSF2(rawSF2);
        musicPlayer.Play(&midiBuf[0], midiBuf.size());

        delete[] rawSF2;
        delete sf2;
        delete midi;
    }
    else
        QListView::keyPressEvent(e);
}