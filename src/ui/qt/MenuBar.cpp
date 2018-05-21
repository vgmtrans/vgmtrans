/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QApplication>
#include <QMessageBox>
#include "MenuBar.h"

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent) {
  AppendFileMenu();
  AppendInfoMenu();
}

void MenuBar::AppendFileMenu() {
  QMenu* file_dropdown = addMenu(tr("File"));
  menu_open_file = file_dropdown->addAction(tr("Open"));
  menu_open_file->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(menu_open_file, &QAction::triggered, this, &MenuBar::OpenFile);

  file_dropdown->addSeparator();

  menu_app_exit = file_dropdown->addAction(tr("Exit"));
  menu_app_exit->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  connect(menu_app_exit, &QAction::triggered, this, &MenuBar::Exit);
}

void MenuBar::AppendInfoMenu() {
  QMenu* info_dropdown = addMenu(tr("Help"));
  menu_about_dlg = info_dropdown->addAction(tr("About VGMTrans"));
  connect(menu_about_dlg, &QAction::triggered, [=] {
    QMessageBox::about(QApplication::activeWindow(), "About VGMTrans", "Copyright (c) 2018 - The VGMTrans Team");
  });
}