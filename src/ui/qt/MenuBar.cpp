/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QMessageBox>
#include <MusicPlayer.h>
#include "MenuBar.h"

MenuBar::MenuBar(QWidget *parent) : QMenuBar(parent) {
  AppendFileMenu();
  AppendOptionsMenu();
  AppendInfoMenu();
}

void MenuBar::AppendFileMenu() {
  QMenu *file_dropdown = addMenu("File");
  menu_open_file = file_dropdown->addAction("Open");
  menu_open_file->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(menu_open_file, &QAction::triggered, this, &MenuBar::OpenFile);

  file_dropdown->addSeparator();

  menu_app_exit = file_dropdown->addAction("Exit");
  menu_app_exit->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  connect(menu_app_exit, &QAction::triggered, this, &MenuBar::Exit);
}

void MenuBar::AppendOptionsMenu() {
  QMenu *options_dropdown = addMenu("Options");
  auto audio_backend = options_dropdown->addMenu("Player audio driver");
  menu_drivers = new QActionGroup(this);

  for (auto &driver : MusicPlayer::the().getAvailableDrivers()) {
    auto driveropt = audio_backend->addAction(QString(driver));
    menu_drivers->addAction(driveropt);

    driveropt->setCheckable(true);
    if (MusicPlayer::the().checkSetting("audio.driver", driver)) {
      driveropt->setChecked(true);
      auto font = driveropt->font();
      font.setBold(true);
      driveropt->setFont(font);
    }
  }

  connect(menu_drivers, &QActionGroup::triggered, [](QAction *driver) {
    MusicPlayer::the().setAudioDriver(driver->text().toStdString().c_str());
  });

  menu_logger_show = options_dropdown->addAction("Show logs");
  menu_logger_show->setCheckable(true);
  menu_logger_show->setChecked(true);
  connect(menu_logger_show, &QAction::triggered, this, &MenuBar::ShowLogger);
}

void MenuBar::AppendInfoMenu() {
  QMenu *info_dropdown = addMenu("Help");
  menu_about_dlg = info_dropdown->addAction("About VGMTrans");
  connect(menu_about_dlg, &QAction::triggered, this, &MenuBar::ShowAbout);
}

void MenuBar::SetLoggerHidden() {
  menu_logger_show->setChecked(false);
}