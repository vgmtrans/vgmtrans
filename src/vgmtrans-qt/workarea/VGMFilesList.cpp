/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QHeaderView>
#include <QMenu>

#include "../util/Helpers.h"
#include "../QtVGMRoot.h"
#include "VGMFilesList.h"
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

    VGMFile *vgmfile = qtVGMRoot.vVGMFile.at(static_cast<unsigned long>(index.row()));

    switch (index.column()) {
        case Property::Name: {
            if (role == Qt::DisplayRole) {
                return QString::fromStdWString(*vgmfile->GetName());
            } else if (role == Qt::DecorationRole) {
                return iconForFileType(vgmfile->GetFileType());
            }
        }

        case Property::Type: {
            if (role == Qt::DisplayRole) {
                switch (vgmfile->GetFileType()) {
                    case FILETYPE_SEQ:
                        return "Sequence";

                    case FILETYPE_INSTRSET:
                        return "Instrument set";

                    case FILETYPE_SAMPCOLL:
                        return "Sample collection";

                    default:
                        return "Unknown";
                };
            }
        }

        case Property::Format: {
            if (role == Qt::DisplayRole) {
                return QString::fromStdString(vgmfile->GetFormatName());
            }
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

        case Property::Type: {
            return "Type";
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

    return 3;
}

void VGMFilesListModel::AddVGMFile() {
    int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
    beginInsertRows(QModelIndex(), position, position);
    endInsertRows();
}

void VGMFilesListModel::RemoveVGMFile() {
    int position = static_cast<int>(qtVGMRoot.vVGMFile.size()) - 1;
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

void VGMFilesList::ItemMenu(const QPoint &pos) {
    auto element = indexAt(pos);
    if (!element.isValid())
        return;

    VGMFile *pointed_vgmfile = qtVGMRoot.vVGMFile[element.row()];
    if (!pointed_vgmfile) {
        return;
    }

    QMenu *vgmfile_menu = new QMenu();
    std::vector<const wchar_t *> *menu_item_names = pointed_vgmfile->GetMenuItemNames();
    for (auto &menu_item : *menu_item_names) {
        vgmfile_menu->addAction(QString::fromStdWString(menu_item));
    }

    QAction *performed_action = vgmfile_menu->exec(mapToGlobal(pos));
    int action_index = 0;
    for (auto &action : vgmfile_menu->actions()) {
        if (performed_action == action) {
            pointed_vgmfile->CallMenuItem(pointed_vgmfile, action_index);
            break;
        }
        action_index++;
    }

    vgmfile_menu->deleteLater();
}

void VGMFilesList::keyPressEvent(QKeyEvent *input) {
    switch (input->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace: {
            if (!selectionModel()->hasSelection())
                return;

            QModelIndexList list = selectionModel()->selectedRows();
            for (auto &index : list) {
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
    MdiArea::Instance()->RemoveView(file);
    view_model->RemoveVGMFile();
}

void VGMFilesList::RequestVGMFileView(QModelIndex index) {
    MdiArea::Instance()->NewView(qtVGMRoot.vVGMFile[index.row()]);
}