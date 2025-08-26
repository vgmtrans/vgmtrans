/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MenuBar.h"

#include <QActionGroup>
#include <QDockWidget>
#include <vector>
#include "Options.h"
#include "Root.h"
#include "LogManager.h"
#include "services/Settings.h"

MenuBar::MenuBar(QWidget *parent, const QList<QDockWidget *> &dockWidgets) : QMenuBar(parent) {
  appendFileMenu();
  appendDetectionMenu();
  appendConversionMenu();
  appendWindowsMenu(dockWidgets);
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

void MenuBar::appendConversionMenu() {
  QMenu *options_dropdown = addMenu("Conversion");
  auto bs = options_dropdown->addMenu("Bank select style");

  auto bankSelectStyle = Settings::the()->conversion.bankSelectStyle();

  QActionGroup *bs_grp = new QActionGroup(this);
  auto act = bs->addAction("GS (Default)");
  act->setCheckable(true);
  act->setChecked(bankSelectStyle == BankSelectStyle::GS);
  bs_grp->addAction(act);
  act = bs->addAction("MMA");
  act->setCheckable(true);
  act->setChecked(bankSelectStyle == BankSelectStyle::MMA);
  bs_grp->addAction(act);

  connect(bs_grp, &QActionGroup::triggered, [](const QAction *bs_style) {
    if (auto text = bs_style->text(); text == "GS (Default)") {
      Settings::the()->conversion.setBankSelectStyle(BankSelectStyle::GS);
    } else if (text == "MMA") {
      Settings::the()->conversion.setBankSelectStyle(BankSelectStyle::MMA);
      L_WARN("MMA style (CC0 * 128 + CC32) bank select was chosen and "
             "it will be used for bank select events in generated MIDIs. This "
             "will cause in-program playback to sound incorrect!");
    }
  });

  act = options_dropdown->addAction("Skip MIDI channel 10");
  act->setCheckable(true);
  act->setChecked(Settings::the()->conversion.skipChannel10());
  connect(act, &QAction::toggled,
[](bool skip) {
  if (!skip)
    pRoot->UI_toast("Tracks using MIDI channel 10 will be silent during in-app playback.", ToastType::Info);
    Settings::the()->conversion.setSkipChannel10(skip);
  });

  options_dropdown->addSeparator();
}

void MenuBar::appendDetectionMenu() {
  QMenu *options_dropdown = addMenu("Detection");
  auto minSizeMenu = options_dropdown->addMenu("Minimum sequence size");

  const int currentSize = Settings::the()->detection.minSequenceSize();

  QActionGroup *sizeGroup = new QActionGroup(this);
  const std::vector<int> sizes = {0, 0x80, 0x100, 0x200, 0x500, 0x1000};
  for (int size : sizes) {
    QString text = size == 0 ? QString("No minimum") :
                               QString("0x%1 bytes").arg(size, 0, 16);
    QAction *act = minSizeMenu->addAction(text);
    act->setCheckable(true);
    act->setChecked(currentSize == size);
    act->setData(size);
    sizeGroup->addAction(act);
  }

  connect(sizeGroup, &QActionGroup::triggered, [](const QAction *action) {
    Settings::the()->detection.setMinSequenceSize(action->data().toInt());
  });
}

void MenuBar::appendWindowsMenu(const QList<QDockWidget *> &dockWidgets) {
  QMenu *windowsDropdown = addMenu("Windows");

  for (auto &widget : dockWidgets) {
    windowsDropdown->addAction(widget->toggleViewAction());
  }
}


void MenuBar::appendInfoMenu() {
  QMenu *info_dropdown = addMenu("Help");
  menu_about_dlg = info_dropdown->addAction("About VGMTrans");
  connect(menu_about_dlg, &QAction::triggered, this, &MenuBar::showAbout);
}
