/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMenu>
#include <QMenuBar>

class MenuBar final : public QMenuBar {
  Q_OBJECT

public:
  explicit MenuBar(QWidget *parent = nullptr);

signals:
  void OpenFile();
  void Exit();
  void ShowAbout();

private:
  void AppendFileMenu();
  void AppendOptionsMenu();
  void AppendInfoMenu();

  // File actions
  QAction *menu_open_file;
  QAction *menu_app_exit;

  // Info actions
  QAction *menu_about_dlg;

  // Options actions
  QActionGroup *menu_drivers;
};