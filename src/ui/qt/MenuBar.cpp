/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "MenuBar.h"

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent) {
  AppendFileMenu();
}

void MenuBar::AppendFileMenu() {
  QMenu* file_dropdown = addMenu(tr("File"));
  menu_open_file = file_dropdown->addAction(tr("Open"), this, &MenuBar::OpenFile, QKeySequence(QStringLiteral("Ctrl+O")));
  
  file_dropdown->addSeparator();

  menu_app_exit = file_dropdown->addAction(tr("Exit"), this, &MenuBar::Exit, QKeySequence(QStringLiteral("Alt+F4")));

}