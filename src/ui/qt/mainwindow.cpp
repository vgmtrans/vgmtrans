/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "QtVGMRoot.h"
#include "MainWindow.h"

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setUnifiedTitleAndToolBarOnMac(true);
  setAcceptDrops(true);

  CreateElements();
  RouteMenuSignals();
}

void MainWindow::CreateElements() {
  ui_menu_bar = new MenuBar(this);
  ui_rawfiles_list = new RawFileListView();
  ui_vgmfiles_list = new VGMFileListView();
  ui_colls_list = new VGMCollListView();

  vertical_splitter = new QSplitter(Qt::Vertical, this);
  horizontal_splitter = new QSplitter(Qt::Horizontal, vertical_splitter);
  vertical_splitter_left = new QSplitter(Qt::Vertical, horizontal_splitter);

  vertical_splitter->addWidget(ui_colls_list);
  vertical_splitter->setHandleWidth(1);

  horizontal_splitter->addWidget(MdiArea::getInstance());
  horizontal_splitter->setHandleWidth(1);

  vertical_splitter_left->addWidget(ui_rawfiles_list);
  vertical_splitter_left->addWidget(ui_vgmfiles_list);
  vertical_splitter_left->setHandleWidth(1);
  
  setCentralWidget(vertical_splitter);
}

void MainWindow::RouteMenuSignals() {
  setMenuBar(ui_menu_bar);
  connect(ui_menu_bar, &MenuBar::OpenFile, this, &MainWindow::OpenFile);
  connect(ui_menu_bar, &MenuBar::Exit, this, &MainWindow::close);
}

void MainWindow::OpenFile() {
  qtVGMRoot.OpenRawFile(qtVGMRoot.UI_GetOpenFilePath());
}