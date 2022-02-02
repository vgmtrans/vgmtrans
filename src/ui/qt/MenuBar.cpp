/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MenuBar.h"

#include <QActionGroup>
#include <QDockWidget>
#include "Options.h"
#include "Root.h"

MenuBar::MenuBar(QWidget *parent, const QList<QDockWidget *> &dockWidgets) : QMenuBar(parent) {
  appendFileMenu();
  appendOptionsMenu(dockWidgets);
  appendInfoMenu();
}

void MenuBar::appendFileMenu() {
  QMenu *file_dropdown = addMenu("File");
  menu_open_file = file_dropdown->addAction("Open");
  menu_open_file->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(menu_open_file, &QAction::triggered, this, &MenuBar::openFile);

  file_dropdown->addSeparator();

  menu_app_exit = file_dropdown->addAction("Exit");
  menu_app_exit->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  connect(menu_app_exit, &QAction::triggered, this, &MenuBar::exit);
}

void MenuBar::appendOptionsMenu(const QList<QDockWidget *> &dockWidgets) {
  QMenu *options_dropdown = addMenu("Options");
  auto bs = options_dropdown->addMenu("Bank select style");

  QActionGroup *bs_grp = new QActionGroup(this);
  auto act = bs->addAction("GS (Default)");
  act->setCheckable(true);
  act->setChecked(true);
  bs_grp->addAction(act);
  act = bs->addAction("MMA");
  act->setCheckable(true);
  bs_grp->addAction(act);

  connect(bs_grp, &QActionGroup::triggered, [](QAction *bs_style) {
    if (auto text = bs_style->text(); text == "GS (Default)") {
      ConversionOptions::the().SetBankSelectStyle(BankSelectStyle::GS);
    } else if (text == "MMA") {
      ConversionOptions::the().SetBankSelectStyle(BankSelectStyle::MMA);
      pRoot->UI_AddLogItem(
          new LogItem(L"MMA style (CC0 * 128 + CC32) bank select was chosen and "
                      L"it will be used for bank select events in generated MIDIs. This "
                      L"will cause in-program playback to sound incorrect!",
                      LOG_LEVEL_WARN, L"VGMTransQt"));
    }
  });

  options_dropdown->addSeparator();

  for (auto &widget : dockWidgets) {
    options_dropdown->addAction(widget->toggleViewAction());
  }
}

void MenuBar::appendInfoMenu() {
  QMenu *info_dropdown = addMenu("Help");
  menu_about_dlg = info_dropdown->addAction("About VGMTrans");
  connect(menu_about_dlg, &QAction::triggered, this, &MenuBar::showAbout);
}
