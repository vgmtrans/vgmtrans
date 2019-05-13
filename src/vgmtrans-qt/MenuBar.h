/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QMenu>
#include <QMenuBar>

class MenuBar : public QMenuBar {
  Q_OBJECT

public:
  explicit MenuBar(QWidget* parent = nullptr);

  inline bool IsLoggerToggled() { return menu_toggle_logger->isChecked(); };
  inline void SetLoggerHidden() { menu_toggle_logger->setChecked(false); };

signals:
  void OpenFile();
  void Exit();
  void LoggerToggled();

private:
  void AppendFileMenu();
  void AppendOptionsMenu();
  void AppendInfoMenu();

  // File actions
  QAction* menu_open_file;
  QAction* menu_app_exit;

  // Info actions
  QAction* menu_about_dlg;

  // Options actions
  QAction* menu_toggle_logger;
};
