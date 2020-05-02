#include "VGMCollView.h"
#include "../QtVGMRoot.h"
#include "../util/Helpers.h"
#include "MdiArea.h"

#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSeq.h>
#include <VGMSampColl.h>
#include <VGMMiscFile.h>
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
    auto file = fileFromIndex(index);
    if (role == Qt::DisplayRole) {
        return QString::fromStdString(
            std::visit([](auto file) -> std::string { return file->name(); }, file));
    } else if (role == Qt::DecorationRole) {
        static Visitor icon{
            [](VGMSeq *) -> const QIcon & {
                static QIcon i_gen{":/images/sequence-32.png"};
                return i_gen;
            },
            [](VGMInstrSet *) -> const QIcon & {
                static QIcon i_gen{":/images/instrument-set-32.png"};
                return i_gen;
            },
            [](VGMSampColl *) -> const QIcon & {
                static QIcon i_gen{":/images/sample-set-32.png"};
                return i_gen;
            },
            [](VGMMiscFile *) -> const QIcon & {
                static QIcon i_gen{":/images/generic-32.png"};
                return i_gen;
            },
        };

        return std::visit(icon, file);
    }

    return QVariant();
}

void VGMCollViewModel::handleNewCollSelected(QModelIndex modelIndex) {
    if (!modelIndex.isValid() || qtVGMRoot.vVGMColl.empty() ||
        modelIndex.row() >= qtVGMRoot.vVGMColl.size()) {
        m_coll = nullptr;
    } else {
        m_coll = qtVGMRoot.vVGMColl[modelIndex.row()];
    }

    emit dataChanged(index(0, 0), index(0, 0));
}

std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> VGMCollViewModel::fileFromIndex(
    QModelIndex index) const {
    auto row = index.row();
    auto num_instrsets = m_coll->instrsets.size();
    auto num_sampcolls = m_coll->sampcolls.size();
    auto num_miscfiles = m_coll->miscfiles.size();

    if (row < num_miscfiles) {
        return m_coll->miscfiles[row];
    }

    row -= num_miscfiles;
    if (row < num_instrsets) {
        return m_coll->instrsets[row];
    }

    row -= num_instrsets;
    if (row < num_sampcolls) {
        return m_coll->sampcolls[row];
    } else {
        return m_coll->seq;
    }
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
    auto file_to_open = qobject_cast<VGMCollViewModel *>(model())->fileFromIndex(index);
    MdiArea::Instance()->NewView(
        std::visit([](auto file) -> VGMFile * { return file; }, file_to_open));
}
