/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "NotificationCenter.h"
#include "VGMItem.h"
#include "Helpers.h"

NotificationCenter::NotificationCenter(QObject *parent) : QObject(parent) {
}

void NotificationCenter::updateStatus(const QString& name, const QString& description, const QIcon* icon, uint32_t offset, uint32_t size) {
  emit statusUpdated(name, description, icon, offset, size);
}

void NotificationCenter::updateStatusForItem(VGMItem* item) {
  if (!item) {
    emit statusUpdated("", "", nullptr, -1, -1);
    return;
  }

  QString name = QString::fromStdString(item->name());
  QString description = QString::fromStdString(item->description());

  QString formattedName = QString{"<b>%1</b>"}.arg(name);
  QIcon icon = iconForItemType(item->type);

  emit statusUpdated(formattedName, description, &icon, item->dwOffset, item->unLength);
}

void NotificationCenter::selectVGMFile(VGMFile* vgmfile, QWidget* caller) {
  emit vgmFileSelected(vgmfile, caller);
}

void NotificationCenter::setSeekModifierActive(bool active) {
  emit seekModifierChanged(active);
}

void NotificationCenter::updateContextualMenusForVGMFiles(const QList<VGMFile*>& files) {
  emit vgmFileContextCommandsChanged(files);
}

void NotificationCenter::updateContextualMenusForVGMColls(const QList<VGMColl*>& colls) {
  emit vgmCollContextCommandsChanged(colls);
}

void NotificationCenter::updateContextualMenusForRawFiles(const QList<RawFile*>& files) {
  emit rawFileContextCommandsChanged(files);
}
