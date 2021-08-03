/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMdiSubWindow>
#include <QSplitter>
#include <QBuffer>

class VGMFile;
class HexView;
class QHexView;
class VGMFileTreeView;

class VGMFileView final : public QMdiSubWindow {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);

private:
  void closeEvent(QCloseEvent *closeEvent) override;
  void markEvents();

  VGMFileTreeView *m_treeview{};
  VGMFile *m_vgmfile{};
  QHexView *m_hexview{};
  QSplitter *m_splitter;
};
