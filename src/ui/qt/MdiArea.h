/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMdiArea>
#include "VGMFileView.h"

class QAbstractButton;

class MdiArea : public QMdiArea {
  Q_OBJECT

public:
  explicit MdiArea(QWidget *parent = nullptr);

public slots:
  void RemoveTab(VGMFileView *file_view);

protected:
  QTabBar *getTabBar();
  QAbstractButton *getCloseButton();
};
