/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QMenu>
#include <QMenuBar>

#ifndef MENUBAR_H
#define MENUBAR_H

class MenuBar : public QMenuBar {
  Q_OBJECT

public:
  explicit MenuBar(QWidget* parent = nullptr);
  ~MenuBar() = default;

signals:
  void OpenFile();
  void Exit();

private:
  void AppendFileMenu();

  // File actions
  QAction* menu_open_file;
  QAction* menu_app_exit;

};

#endif // !MAINWINDOW_H
