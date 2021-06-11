/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QMainWindow>
#include "workarea/VGMFileView.h"

class QSplitter;
class QListView;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();

  static MainWindow &getInstance() {
    static MainWindow instance;
    return instance;
  }

protected:
  QSplitter *vertSplitter;
  QSplitter *horzSplitter;
  QSplitter *vertSplitterLeft;
  QListView *rawFileListView;
  QListView *vgmFileListView;
  QListView *vgmCollListView;
  QListView *collListView;

protected:
  void dragEnterEvent(QDragEnterEvent *event);
  void dragMoveEvent(QDragMoveEvent *event);
  void dropEvent(QDropEvent *event);
};
