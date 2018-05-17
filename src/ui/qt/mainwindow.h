/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QDropEvent>
#include <QDragEnterEvent>

#include "MenuBar.h"
#include "RawFileListView.h"
#include "VGMFileListView.h"
#include "VGMCollListView.h"
#include "MdiArea.h"

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow();
  ~MainWindow() = default;

private:
  void CreateElements();
  void RouteMenuSignals();

  void OpenFile();

  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

  MenuBar* ui_menu_bar;
  RawFileListView* ui_rawfiles_list;
  VGMFileListView* ui_vgmfiles_list;
  VGMCollListView* ui_colls_list;

  QSplitter* vertical_splitter;
  QSplitter* horizontal_splitter;
  QSplitter* vertical_splitter_left;
};

#endif // !MAINWINDOW_H
