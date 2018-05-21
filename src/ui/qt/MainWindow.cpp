/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QFileInfo>
#include <QMimeData>
#include <QLabel>
#include "QtVGMRoot.h"
#include "MainWindow.h"

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setUnifiedTitleAndToolBarOnMac(true);
  setAcceptDrops(true);

  CreateElements();
  RouteSignals();
}

void MainWindow::CreateElements() {
  ui_menu_bar = new MenuBar(this);
  ui_iconbar = new IconBar(this);
  addToolBar(ui_iconbar);
  ui_rawfiles_list = new RawFileListView(this);
  ui_vgmfiles_list = new VGMFileListView(this);
  ui_colls_list = new VGMCollListView(this);
  ui_tabs_area = new MdiArea(this);

  vertical_splitter = new QSplitter(Qt::Vertical, this);
  horizontal_splitter = new QSplitter(Qt::Horizontal, vertical_splitter);
  vertical_splitter_left = new QSplitter(Qt::Vertical, horizontal_splitter);

  vertical_splitter->addWidget(ui_colls_list);
  vertical_splitter->setHandleWidth(1);

  horizontal_splitter->addWidget(ui_tabs_area);
  horizontal_splitter->setHandleWidth(1);

  vertical_splitter_left->addWidget(ui_rawfiles_list);
  vertical_splitter_left->addWidget(ui_vgmfiles_list);
  vertical_splitter_left->setHandleWidth(1);
  
  setCentralWidget(vertical_splitter);
}

void MainWindow::RouteSignals() {
  setMenuBar(ui_menu_bar);
  connect(ui_menu_bar, &MenuBar::OpenFile, this, &MainWindow::OpenFile);
  connect(ui_menu_bar, &MenuBar::Exit, this, &MainWindow::close);
  connect(ui_iconbar, &IconBar::OpenPressed, this, &MainWindow::OpenFile);
  connect(ui_vgmfiles_list, &VGMFileListView::AddMdiTab, ui_tabs_area, &MdiArea::addSubWindow);
}

void MainWindow::OpenFile() {
  qtVGMRoot.OpenRawFile(qtVGMRoot.UI_GetOpenFilePath());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
  const auto& files = event->mimeData()->urls();

  if(files.isEmpty())
    return;

  for (const auto& file : files) {
    // Leave sanity checks to the backend
    qtVGMRoot.OpenRawFile(
      QFileInfo(file.toLocalFile()).filePath().toStdWString());
  }
}