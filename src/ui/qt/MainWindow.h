/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QMainWindow>
#include "workarea/VGMFileView.h"

class QDockWidget;
class MenuBar;
class IconBar;
class Logger;
class VGMCollListView;
class VGMCollView;
class QPushButton;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void createElements();
  void routeSignals();

  void OpenFile();

  QDockWidget *m_rawfile_dock{};
  QDockWidget *m_vgmfile_dock{};
  MenuBar *m_menu_bar{};
  IconBar *m_icon_bar{};
  Logger *m_logger{};
  VGMCollListView *m_coll_listview{};
  VGMCollView *m_coll_view{};
  QPushButton *m_manual_creation_btn{};
};
