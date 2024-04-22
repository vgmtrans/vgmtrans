/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QObject>

class VGMItem;
class VGMFile;

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
                    int offset = -1,
                    int size = -1);
  void updateStatusForItem(VGMItem* item);
  void selectVGMFile(VGMFile* vgmfile, QWidget* caller);

private:
  explicit NotificationCenter(QObject *parent = nullptr);

signals:
  void statusUpdated(const QString& name, const QString& description, const QIcon* icon, int offset, int size);
  void vgmFileSelected(VGMFile *file, QWidget* caller);
};