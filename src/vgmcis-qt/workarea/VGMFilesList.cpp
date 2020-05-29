/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFilesList.h"

#include <iostream>
#include <QHeaderView>
#include <QMenu>
#include <variant>

#include "components/instr/VGMInstrSet.h"
#include "components/seq/VGMSeq.h"
#include "components/VGMSampColl.h"
#include "components/VGMMiscFile.h"
#include "conversion/JSONDump.h"
#include "../util/Helpers.h"
#include "../QtVGMRoot.h"
#include "MdiArea.h"

/*
 *  VGMFilesListModel
 */

VGMFilesListModel::VGMFilesListModel(QObject *parent) : QAbstractTableModel(parent) {
    connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMFile, this, &VGMFilesListModel::AddVGMFile);
    connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedVGMFile, this, &VGMFilesListModel::RemoveVGMFile);
}

QVariant VGMFilesListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }

    auto vgmfile = qtVGMRoot.vVGMFile.at(static_cast<unsigned long>(index.row()));

    switch (index.column()) {
        case Property::Name: {
            if (role == Qt::DisplayRole) {
                return QString::fromStdString(
                        std::visit([](auto file) -> std::string { return *file->GetName(); }, vgmfile));
            } else if (role == Qt::DecorationRole) {
                static Visitor icon{
                        [](VGMSeq *) -> const QIcon & {
                            static QIcon i_gen{":/images/sequence.svg"};
                            return i_gen;
                        },
                        [](VGMInstrSet *) -> const QIcon & {
                            static QIcon i_gen{":/images/instrument-set.svg"};
                            return i_gen;
                        },
                        [](VGMSampColl *) -> const QIcon & {
                            static QIcon i_gen{":/images/wave.svg"};
                            return i_gen;
                        },
                        [](VGMMiscFile *) -> const QIcon & {
                            static QIcon i_gen{":/images/file.svg"};
                            return i_gen;
                        },
                };

                return std::visit(icon, vgmfile);
            }
            [[fallthrough]];
        }

        case Property::Format: {
            if (role == Qt::DisplayRole) {
                return QString::fromStdString(std::visit(
                        [](auto file) -> std::string { return file->GetFormatName(); }, vgmfile));
            }
            [[fallthrough]];
        }

        default: {
            return QVariant();
        }
    }
}

QVariant VGMFilesListModel::headerData(int column, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Vertical || role != Qt::DisplayRole)
        return QVariant();

    switch (column) {
        case Property::Name: {
            return "Name";
        }

        case Property::Format: {
            return "Format";
        }

        default: {
            return QVariant();
        }
    }
}

int VGMFilesListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;

    return static_cast<int>(qtVGMRoot.vVGMFile.size());
}

int VGMFilesListModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;

    return 2;
}

void VGMFilesListModel::AddVGMFile() {
    int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
    beginInsertRows(QModelIndex(), position, position);
    endInsertRows();
}

void VGMFilesListModel::RemoveVGMFile() {
    int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
    if (position < 0) {
        return;
    }

    beginRemoveRows(QModelIndex(), position, position);
    endRemoveRows();
}

/*
 *  VGMFilesList
 */

VGMFilesList::VGMFilesList(QWidget *parent) : QTableView(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setSortingEnabled(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setWordWrap(false);

    // auto *proxy_model = new QSortFilterProxyModel(this);
    view_model = new VGMFilesListModel();
    // proxy_model->setSourceModel(view_model);
    setModel(view_model);

    verticalHeader()->hide();
    auto header_hor = horizontalHeader();
    header_hor->setSectionsMovable(true);
    header_hor->setHighlightSections(true);
    header_hor->setSectionResizeMode(QHeaderView::Stretch);

    connect(&qtVGMRoot, &QtVGMRoot::UI_RemoveVGMFile, this, &VGMFilesList::RemoveVGMFile);
    connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMFilesList::ItemMenu);
    connect(this, &QAbstractItemView::doubleClicked, this, &VGMFilesList::RequestVGMFileView);
}

static struct {
    QMenu *operator()(VGMSeq *seq) {
        auto menu = new QMenu;
        menu->addAction("Save as MIDI", [seq]() {
            auto save_path = qtVGMRoot.UI_GetSaveFilePath(seq->name());
            if (save_path.empty()) {
                return;
            }

            auto midi = seq->ConvertToMidi();
            midi->SaveMidiFile(save_path);
            delete midi;
        });
        menu->addAction("Save as JSON", [seq]() {
            auto save_path = qtVGMRoot.UI_GetSaveFilePath(seq->name());
            if (save_path.empty()) {
                return;
            }

            conversion::DumpToJSON(*seq, save_path);
        });

        return menu;
    }

    QMenu *operator()(VGMInstrSet *set) {
        auto menu = new QMenu;
        menu->addAction("Save as DLS", [set]() {
            auto save_path = qtVGMRoot.UI_GetSaveFilePath(set->name());
            if (save_path.empty()) {
                return;
            }

            conversion::SaveAsDLS(*set, save_path);
        });
        menu->addAction("Save as SF2", [set]() {
            auto save_path = qtVGMRoot.UI_GetSaveFilePath(set->name());
            if (save_path.empty()) {
                return;
            }

            conversion::SaveAsSF2(*set, save_path);
        });

        return menu;
    }

    QMenu *operator()(VGMSampColl *coll) {
        auto menu = new QMenu;
        menu->addAction("Save samples as WAV", [coll]() {
            auto save_path = qtVGMRoot.UI_GetSaveDirPath();
            if (save_path.empty()) {
                return;
            }

            conversion::SaveAsWAV(*coll, save_path);
        });

        return menu;
    }

    QMenu *operator()(VGMMiscFile *) { return new QMenu; }
} MakeMenu;

void VGMFilesList::ItemMenu(const QPoint &pos) {
    auto element = indexAt(pos);
    if (!element.isValid())
        return;

    auto selected_file = qtVGMRoot.vVGMFile[element.row()];
    auto menu = std::visit(MakeMenu, selected_file);
    std::visit(
            [&menu](auto file) mutable {
                menu->addSeparator();
                menu->addAction("Save raw data", [file]() {
                    auto save_path = qtVGMRoot.UI_GetSaveFilePath(file->name());
                    if (save_path.empty()) {
                        return;
                    }

                    std::ofstream out{save_path, std::ios::out | std::ofstream::binary};
                    std::copy(file->rawfile->begin() + file->dwOffset,
                              file->rawfile->begin() + file->dwOffset + file->unLength,
                              std::ostreambuf_iterator<char>(out));
                });
                menu->addAction("Remove file", [file]() {
                    MdiArea::Instance()->RemoveView(file);
                    qtVGMRoot.RemoveVGMFile(file);
                });
            },
            selected_file);

    menu->exec(mapToGlobal(pos));
    menu->deleteLater();
}

void VGMFilesList::keyPressEvent(QKeyEvent *input) {
    switch (input->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace: {
            if (!selectionModel()->hasSelection())
                return;

            QModelIndexList list = selectionModel()->selectedRows();
            for (auto &index : list) {
                MdiArea::Instance()->RemoveView(qtVGMRoot.vVGMFile[index.row()]);
                qtVGMRoot.RemoveVGMFile(qtVGMRoot.vVGMFile[index.row()]);
            }

            return;
        }

            // Pass the event back to the base class, needed for keyboard navigation
        default:
            QTableView::keyPressEvent(input);
    }
}

void VGMFilesList::RemoveVGMFile(VGMFile *file) {
    view_model->RemoveVGMFile();
}

void VGMFilesList::RequestVGMFileView(QModelIndex index) {
    MdiArea::Instance()->NewView(qtVGMRoot.vVGMFile[index.row()]);
}
