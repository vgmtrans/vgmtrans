/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QStatusBar>
#include <QLabel>

#include "MenuBar.h"
#include "RawFileListView.h"
#include "VGMFilesList.h"
#include "VGMCollListView.h"
#include "MdiArea.h"
#include "IconBar.h"
#include "Logger.h"

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow();

private:
  void CreateElements();
  void RouteSignals();

  void OpenFile();

  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

  MenuBar* ui_menu_bar;
  RawFileListView* ui_rawfiles_list;
  VGMFilesList* ui_vgmfiles_list;
  VGMCollListView* ui_colls_list;
  IconBar* ui_iconbar;
  Logger* ui_logger;

  QStatusBar* ui_statusbar;
  QLabel* ui_statusbar_offset;
  QLabel* ui_statusbar_length;

  QSplitter* vertical_splitter;
  QSplitter* horizontal_splitter;
  QSplitter* vertical_splitter_left;
};
