/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSplitter>
#include <QMdiSubWindow>
#include <QCloseEvent>
#include <QGridLayout>

class VGMFile;
class HexView;
class VGMFileTreeView;

class VGMFileView : public QMdiSubWindow {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmfile);

protected:
  QSplitter *filetab_splitter_;
  HexView *filetab_hexview_;
  VGMFileTreeView *filetab_treeview_;
  VGMFile *internal_vgmfile_;

private:
  void closeEvent(QCloseEvent *closeEvent) override;
};
