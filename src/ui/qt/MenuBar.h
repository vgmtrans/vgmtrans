/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMenu>
#include <QMenuBar>
#include <QList>

class QDockWidget;

class MenuBar final : public QMenuBar {
  Q_OBJECT

public:
  explicit MenuBar(QWidget *parent = nullptr, const QList<QDockWidget *>& dockWidgets = {});

signals:
  void openFile();
  void exit();
  void showAbout();

private:
  void appendFileMenu();
  void appendConversionMenu();
  void appendWindowsMenu(const QList<QDockWidget *> &dockWidgets);
  void appendInfoMenu();

  // File actions
  QAction *menu_open_file;
  QAction *menu_app_exit;

  // Info actions
  QAction *menu_about_dlg;

  // Options actions
  QActionGroup *menu_drivers;
};