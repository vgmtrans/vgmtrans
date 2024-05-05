/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "QtVGMRoot.h"

#include <QMainWindow>
#include "workarea/VGMFileView.h"

class QDockWidget;
class MenuBar;
class IconBar;
class Logger;
class VGMCollListView;
class VGMCollView;
class QPushButton;
class StatusBarContent;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  void showEvent(QShowEvent* event) override;

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void createElements();
  void createStatusBar();
  void routeSignals();

  void OpenFile();
  void openFileInternal(QString filename);

  void handleFileProcessed(const QString &filename, bool success);

  QDockWidget *m_rawfile_dock{};
  QDockWidget *m_vgmfile_dock{};
  QDockWidget *m_coll_dock{};
  MenuBar *m_menu_bar{};
  IconBar *m_icon_bar{};
  StatusBarContent *statusBarContent{};
  Logger *m_logger{};
  VGMCollListView *m_coll_listview{};
  VGMCollView *m_coll_view{};
  QPushButton *m_manual_creation_btn{};

  QThread* workerThread;
  Worker* worker;

signals:
  void operate(const QString &filename);
};
