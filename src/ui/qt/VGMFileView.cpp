/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "VGMFileView.h"
#include "VGMFile.h"
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "Helpers.h"

VGMFileView::VGMFileView(VGMFile *vgmFile) : QSplitter(Qt::Horizontal, nullptr) {
  filetab_hexview = new HexView(vgmFile, this);
  filetab_treeview = new VGMFileTreeView(vgmFile, this);

  // FIXME: fixed sizes are meh
  setSizes(QList<int>() << 850 << 100);
  setStretchFactor(1, 1);
  setHandleWidth(1);

  QString vgmFileName = QString::fromStdWString(*vgmFile->GetName());
  setWindowTitle(vgmFileName);
  setWindowIcon(iconForFileType(vgmFile->GetFileType()));
}
