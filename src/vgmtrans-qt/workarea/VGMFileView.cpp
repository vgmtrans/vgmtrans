/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"
#include "VGMFile.h"
#include "HexView.h"
#include "VGMFileTreeView.h"
#include "MdiArea.h"
#include "../util/Helpers.h"

VGMFileView::VGMFileView(VGMFile *vgmfile) : QMdiSubWindow() {
  internal_vgmfile_ = vgmfile;

  filetab_splitter_ = new QSplitter(Qt::Horizontal, this);
  filetab_hexview_ = new HexView(internal_vgmfile_, filetab_splitter_);
  filetab_treeview_ = new VGMFileTreeView(internal_vgmfile_, filetab_splitter_);
  filetab_splitter_->setSizes(QList<int>() << 850 << 100);

  setWindowTitle(QString::fromStdWString(*internal_vgmfile_->GetName()));
  setWindowIcon(iconForFileType(internal_vgmfile_->GetFileType()));
  setAttribute(Qt::WA_DeleteOnClose);

  setWidget(filetab_splitter_);
}

void VGMFileView::closeEvent(QCloseEvent *closeEvent) {
  MdiArea::Instance()->RemoveView(internal_vgmfile_);
}