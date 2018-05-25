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
        return QIcon(":/images/music_folder-32.png");
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
  QPoint absolute_position = mapToGlobal(pos);

  VGMColl *pointed_coll = qtVGMRoot.vVGMColl[indexAt(pos).row()];
  if(pointed_coll == nullptr) {
    return;
  }

  QMenu *vgmcoll_menu = new QMenu();
  std::vector<const wchar_t*>* menu_item_names = pointed_coll->GetMenuItemNames();
  for(auto &menu_item : *menu_item_names) {
    vgmcoll_menu->addAction(QString::fromStdWString(menu_item));
  }

  QAction *performed_action = vgmcoll_menu->exec(absolute_position);
  int action_index = 0;
  for(auto &action : vgmcoll_menu->actions()) {
    if(performed_action == action) {
      pointed_coll->CallMenuItem(pointed_coll, action_index);
      break;
    }
    action_index++;
  }

  vgmcoll_menu->deleteLater();
}
