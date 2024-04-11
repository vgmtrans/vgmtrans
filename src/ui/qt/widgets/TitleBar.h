/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>

class TitleBar : public QWidget {
  Q_OBJECT

public:
  explicit TitleBar(const QString& title, QWidget *parent = nullptr);
  virtual ~TitleBar() = default;
};