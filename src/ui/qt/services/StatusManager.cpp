/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "StatusManager.h"
#include "VGMItem.h"
#include "Helpers.h"

StatusManager::StatusManager(QObject *parent) : QObject(parent) {
}

void StatusManager::updateStatus(const QString& name, const QString& description, const QIcon* icon, int offset, int size) {
  emit updateStatusSignal(name, description, icon, offset, size);
}

void StatusManager::updateStatusForItem(VGMItem* item) {
  if (!item) {
    emit updateStatusSignal("", "", nullptr, -1, -1);
    return;
  }

  QString name = QString::fromStdString(item->name);
  QString description = QString::fromStdString(item->GetDescription());

  QString formattedName = QString{"<b>%1</b>"}.arg(name);
  QIcon icon = iconForItemType(item->GetIcon());

  emit updateStatusSignal(formattedName, description, &icon, item->dwOffset, item->unLength);
}