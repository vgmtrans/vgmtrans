/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWidget>
#include <QLabel>

class StatusBarContent : public QWidget {
  Q_OBJECT

public:
  explicit StatusBarContent(QWidget *parent = nullptr);

public slots:
  void setStatus(const QString& name, const QString& description, const QIcon* icon = nullptr, int offset = -1, int size = -1);

private:
  QLabel* iconLabel;
  QLabel* nameLabel;
  QLabel* descriptionLabel;
  QLabel* offsetLabel;
  QLabel* sizeLabel;
};
