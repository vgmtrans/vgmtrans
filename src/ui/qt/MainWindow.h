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

class MenuBar;

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

  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void createElements();
  void routeSignals();

  void OpenFile();

  MenuBar *m_menu_bar{};
};
