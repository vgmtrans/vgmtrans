/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QBuffer>

class VGMFile;
class HexView;
class QHexView;
class VGMFileTreeView;

class VGMFileView final : public QSplitter {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);

  void addToMdi();

private:
  void markEvents();

  VGMFileTreeView *m_treeview{};
  VGMFile *m_vgmfile{};
  QHexView *m_hexview{};
  QBuffer m_buffer{};
};
