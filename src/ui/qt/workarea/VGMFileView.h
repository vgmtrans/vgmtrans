/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMdiSubWindow>

class SnappingSplitter;
class VGMFile;
class HexView;
class VGMFileTreeView;
class VGMItem;
class QScrollArea;

class VGMFileView final : public QMdiSubWindow {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);

private:
  static constexpr int treeViewMinimumWidth = 220;

  void resetSnapRanges();
  void focusInEvent(QFocusEvent* event) override;
  void closeEvent(QCloseEvent *closeEvent) override;
  int hexViewWidth();
  int hexViewWidthSansAscii();
  int hexViewWidthSansAsciiAndAddress();
  void updateHexViewFont(qreal sizeIncrement);

  VGMFileTreeView* m_treeview{};
  VGMFile* m_vgmfile{};
  QScrollArea* m_hexScrollArea;
  HexView* m_hexview{};
  SnappingSplitter* m_splitter;

public slots:
  void onSelectionChange(VGMItem* item);
};
