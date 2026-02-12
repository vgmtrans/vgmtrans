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
class RawFile;

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
  void setSeekModifierActive(bool active);
  void updateContextualMenusForVGMFiles(const QList<VGMFile*>& files);
  void updateContextualMenusForVGMColls(const QList<VGMColl*>& colls);
  void updateContextualMenusForRawFiles(const QList<RawFile*>& files);

  void vgmfiletree_setShowDetails(bool showDetails) { emit vgmfiletree_showDetailsChanged(showDetails); }

private:
  explicit NotificationCenter(QObject *parent = nullptr);

signals:
  void statusUpdated(const QString& name, const QString& description, const QIcon* icon, int offset, int size);
  void vgmFileSelected(VGMFile *file, QWidget* caller);
  void seekModifierChanged(bool active);
  void vgmFileContextCommandsChanged(const QList<VGMFile*>& files);
  void vgmCollContextCommandsChanged(const QList<VGMColl*>& colls);
  void rawFileContextCommandsChanged(const QList<RawFile*>& files);

  void vgmfiletree_showDetailsChanged(bool showDetails);
};
