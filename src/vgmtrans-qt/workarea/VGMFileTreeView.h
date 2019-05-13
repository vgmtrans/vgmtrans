/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QTreeView>
#include "VGMFileItemModel.h"

class VGMFile;

class VGMFileTreeView : public QTreeView {
  Q_OBJECT

public:
  VGMFileTreeView(VGMFile *vgmfile, QWidget *parent = nullptr);

private:
  VGMFile *vgmfile;
  VGMFileItemModel model;
};
