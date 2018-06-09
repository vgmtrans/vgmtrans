/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QMessageBox>
#include "MenuBar.h"
#include "Logger.h"

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent) {
  AppendFileMenu();
  AppendOptionsMenu();
  AppendInfoMenu();
}

void MenuBar::AppendFileMenu() {
  QMenu* file_dropdown = addMenu("File");
  menu_open_file = file_dropdown->addAction("Open");
  menu_open_file->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(menu_open_file, &QAction::triggered, this, &MenuBar::OpenFile);

  file_dropdown->addSeparator();

  menu_app_exit = file_dropdown->addAction("Exit");
  menu_app_exit->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  connect(menu_app_exit, &QAction::triggered, this, &MenuBar::Exit);
}

void MenuBar::AppendOptionsMenu() {
  QMenu* options_dropdown = addMenu("Options");
  menu_toggle_logger = options_dropdown->addAction("Show logger");
  menu_toggle_logger->setCheckable(true);
  connect(menu_toggle_logger, &QAction::triggered, this, &MenuBar::LoggerToggled);
}

void MenuBar::AppendInfoMenu() {
  QMenu* info_dropdown = addMenu("Help");
  menu_about_dlg = info_dropdown->addAction("About VGMTrans");
  connect(menu_about_dlg, &QAction::triggered, [=] {
    QMessageBox::about(QApplication::activeWindow(), "About VGMTrans",
                       "Copyright (c) 2018 - The VGMTrans Team");
  });
}