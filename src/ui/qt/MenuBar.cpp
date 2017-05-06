#include "MenuBar.h"

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent)
{
  AddFileMenu();


}

void MenuBar::AddFileMenu()
{
  QMenu* file_menu = addMenu(tr("File"));
  openAction = file_menu->addAction(tr("Open"), this, SIGNAL(Open()));
  exitAction = file_menu->addAction(tr("Exit"), this, SIGNAL(Exit()));
}