/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#ifndef VGMTRANS_BREAKDOWNVIEW_H
#define VGMTRANS_BREAKDOWNVIEW_H

#include <QSplitter>

class VGMFile;
class HexView;
class VGMFileTreeView;

class VGMFileView : public QSplitter {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);
  ~VGMFileView() = default;

protected:
  HexView *filetab_hexview;
  VGMFileTreeView *filetab_treeview;
};

#endif //VGMTRANS_BREAKDOWNVIEW_H
