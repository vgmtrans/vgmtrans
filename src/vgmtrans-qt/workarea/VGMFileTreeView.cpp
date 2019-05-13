/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #include "VGMFileTreeView.h"

VGMFileTreeView::VGMFileTreeView(VGMFile *file, QWidget *parent)
    : QTreeView(parent), vgmfile(file), model(file) {
  this->setModel(&model);
}
