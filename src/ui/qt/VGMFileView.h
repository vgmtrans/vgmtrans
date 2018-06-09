/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSplitter>

class VGMFile;
class HexView;
class VGMFileTreeView;

class VGMFileView : public QSplitter {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);

protected:
  HexView *filetab_hexview;
  VGMFileTreeView *filetab_treeview;
};
