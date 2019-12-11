#include "VGMCollView.h"
#include "../QtVGMRoot.h"
#include "../util/Helpers.h"
#include "MdiArea.h"

#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>
#include <VGMColl.h>

VGMCollViewModel::VGMCollViewModel(QItemSelectionModel *collListSelModel, QObject *parent)
    : QAbstractListModel(parent), m_coll(nullptr) {
    QObject::connect(collListSelModel, &QItemSelectionModel::currentChanged, this,
                     &VGMCollViewModel::handleNewCollSelected);
}

int VGMCollViewModel::rowCount(const QModelIndex &parent) const {
    if (m_coll == nullptr) {
        return 0;
    }

    return m_coll->instrsets.size() + m_coll->sampcolls.size() + m_coll->miscfiles.size() + 1;
}

QVariant VGMCollViewModel::data(const QModelIndex &index, int role) const {
    VGMFile *file = fileFromIndex(index);

    if (role == Qt::DisplayRole) {
        return QString::fromStdString(*file->GetName());
    } else if (role == Qt::DecorationRole) {
        return iconForFileType(file->GetFileType());
    }

    return QVariant();
}

void VGMCollViewModel::handleNewCollSelected(QModelIndex modelIndex) {
    if (!modelIndex.isValid() || qtVGMRoot.vVGMColl.empty() ||
        qtVGMRoot.vVGMColl.size() < modelIndex.row()) {
        m_coll = nullptr;
    } else {
        m_coll = qtVGMRoot.vVGMColl[modelIndex.row()];
    }

    emit dataChanged(index(0, 0), index(0, 0));
}

VGMFile *VGMCollViewModel::fileFromIndex(QModelIndex index) const {
    VGMFile *file;
    auto row = index.row();
    auto num_instrsets = m_coll->instrsets.size();
    auto num_sampcolls = m_coll->sampcolls.size();
    auto num_miscfiles = m_coll->miscfiles.size();
    if (row < num_miscfiles) {
        file = m_coll->miscfiles[row];
    } else {
        row -= num_miscfiles;
        if (row < num_sampcolls) {
            file = m_coll->instrsets[row];
        } else {
            row -= num_instrsets;
            if (row < num_sampcolls) {
                file = m_coll->sampcolls[row];
            } else {
                file = m_coll->seq;
            }
        }
    }
    return file;
}

VGMCollView::VGMCollView(QItemSelectionModel *collListSelModel, QWidget *parent)
    : QListView(parent) {
    setAttribute(Qt::WA_MacShowFocusRect, 0);

    VGMCollViewModel *vgmCollViewModel = new VGMCollViewModel(collListSelModel, this);
    this->setModel(vgmCollViewModel);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setSelectionRectVisible(true);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(this, &VGMCollView::doubleClicked, this, &VGMCollView::doubleClickedSlot);
}

void VGMCollView::doubleClickedSlot(QModelIndex index) {
    MdiArea::Instance()->NewView(qobject_cast<VGMCollViewModel *>(model())->fileFromIndex(index));
}
