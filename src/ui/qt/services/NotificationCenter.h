/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QList>
#include <QObject>

class VGMItem;
class VGMFile;
class VGMColl;

class NotificationCenter : public QObject {
  Q_OBJECT

public:
  static auto the() {
    static NotificationCenter* hub = new NotificationCenter();
    return hub;
  }

  NotificationCenter(const NotificationCenter&) = delete;
  NotificationCenter&operator=(const NotificationCenter&) = delete;
  NotificationCenter(NotificationCenter&&) = delete;
  NotificationCenter&operator=(NotificationCenter&&) = delete;

  void updateStatus(const QString& name,
                    const QString& description,
                    const QIcon* icon = nullptr,
                    uint32_t offset = -1,
                    uint32_t size = -1);
  void updateStatusForItem(VGMItem* item);
  void selectVGMFile(VGMFile* vgmfile, QWidget* caller);
  void updateContextualMenusForVGMFiles(const QList<VGMFile*>& files);
  void updateContextualMenusForVGMColls(const QList<VGMColl*>& colls);

  void vgmfiletree_setShowDetails(bool showDetails) { emit vgmfiletree_showDetailsChanged(showDetails); }

private:
  explicit NotificationCenter(QObject *parent = nullptr);

signals:
  void statusUpdated(const QString& name, const QString& description, const QIcon* icon, int offset, int size);
  void vgmFileSelected(VGMFile *file, QWidget* caller);
  void vgmFileContextCommandsChanged(const QList<VGMFile*>& files);
  void vgmCollContextCommandsChanged(const QList<VGMColl*>& colls);

  void vgmfiletree_showDetailsChanged(bool showDetails);
};
