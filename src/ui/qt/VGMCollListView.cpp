/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QEvent>
#include <QMenu>
#include "VGMCollListView.h"
#include "QtVGMRoot.h"
#include "VGMColl.h"

/*
 * VGMCollListViewModel
 */
VGMCollListViewModel::VGMCollListViewModel(QObject *parent)
        : QAbstractListModel(parent)
{
  connect(&qtVGMRoot, &QtVGMRoot::UI_AddedVGMColl, [=]() { dataChanged(index(0, 0), index(0, 0)); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_RemovedVGMColl, [=]() { dataChanged(index(0, 0), index(0, 0)); });
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
        return QIcon(":/images/collection-32.png");
    }
    return QVariant();
}

/*
* VGMCollListView
*/

VGMCollListView::VGMCollListView(QWidget *parent)
        : QListView(parent)
{
    VGMCollListViewModel *vgmCollListViewModel = new VGMCollListViewModel(this);
    setModel(vgmCollListViewModel);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIconSize(QSize(16, 16));

    connect(this, &QAbstractItemView::customContextMenuRequested, this, &VGMCollListView::CollMenu);
}

void VGMCollListView::CollMenu(const QPoint &pos) {
  auto element = indexAt(pos);
  if(element.isValid()) {
    VGMColl *pointed_coll = qtVGMRoot.vVGMColl[element.row()];
    if(pointed_coll == nullptr) {
      return;
    }

    QMenu *vgmcoll_menu = new QMenu();
    std::vector<const wchar_t*>* menu_item_names = pointed_coll->GetMenuItemNames();
    for(auto &menu_item : *menu_item_names) {
      vgmcoll_menu->addAction(QString::fromStdWString(menu_item));
    }

    QAction *performed_action = vgmcoll_menu->exec(mapToGlobal(pos));
    int action_index = 0;
    for(auto &action : vgmcoll_menu->actions()) {
      if(performed_action == action) {
        pointed_coll->CallMenuItem(pointed_coll, action_index);
        break;
      }
      action_index++;
    }

    vgmcoll_menu->deleteLater();
  
  } else if(qtVGMRoot.vVGMColl.size() > 0) {
      QMenu *vgmcoll_menu = new QMenu();
      auto export_all = vgmcoll_menu->addAction("Export all as MIDI, SF2 and DLS");
      if(vgmcoll_menu->exec(mapToGlobal(pos)) == export_all) {
        auto save_path = qtVGMRoot.UI_GetSaveDirPath();
        for(auto &coll : qtVGMRoot.vVGMColl) {
          coll->SetDefaultSavePath(save_path);
          coll->OnSaveAll();
        }
      }

      vgmcoll_menu->deleteLater();
  } else {
    return;
  }
}
