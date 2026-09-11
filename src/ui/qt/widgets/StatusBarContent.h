/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "util/CapsuleText.h"

#include <QLabel>
#include <QWidget>

class CapsuleTextLabel;

class StatusBarContent : public QWidget {
  Q_OBJECT

public:
  explicit StatusBarContent(QWidget *parent = nullptr);

public slots:
  void setStatus(const QString& name, const QString& description, const QIcon* icon = nullptr, int offset = -1, int size = -1) const;
  void setInspectorStatus(const QString& name, const CapsuleText& description,
                          const QIcon* icon = nullptr, int offset = -1, int size = -1) const;

private:
  void setCommonStatus(const QString& name, const QIcon* icon, int offset, int size) const;

  QLabel* iconLabel;
  QLabel* nameLabel;
  CapsuleTextLabel* descriptionLabel;
  QLabel* offsetLabel;
  QLabel* sizeLabel;
};
