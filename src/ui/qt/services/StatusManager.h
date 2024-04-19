/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QObject>

class VGMItem;

class StatusManager : public QObject {
  Q_OBJECT

public:
  static auto the() {
    static StatusManager* area = new StatusManager();
    return area;
  }

  StatusManager(const StatusManager &) = delete;
  StatusManager &operator=(const StatusManager &) = delete;
  StatusManager(StatusManager &&) = delete;
  StatusManager &operator=(StatusManager &&) = delete;

  void updateStatus(const QString& name,
                    const QString& description,
                    const QIcon* icon = nullptr,
                    int offset = -1,
                    int size = -1);
  void updateStatusForItem(VGMItem* item);

private:
  explicit StatusManager(QObject *parent = nullptr);

signals:
  void updateStatusSignal(const QString& name, const QString& description, const QIcon* icon, int offset, int size);
};