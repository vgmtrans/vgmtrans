#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter.h>
#include <QTreeView.h>
#include <QListView.h>
#include <QMdiArea>

#include "ui/qt/VGMFileView.h"

class MenuBar;
class ToolBar;
class HeaderContainer;

class MainWindow final : public QMainWindow
{
  Q_OBJECT

 public:
  explicit MainWindow(QWidget *parent = 0);

 private slots:
  void Open();
  void Exit();

  void Play();
  void Pause();
  void Stop();


 private:
  void CreateComponents();
  void SetupSplitters();
  void ConnectMenuBar();
  void ConnectToolBar();

  MenuBar* menuBar;
  ToolBar* toolBar;
  QSplitter* vertSplitter;
  QSplitter* horzSplitter;
  QSplitter* vertSplitterLeft;
  QListView* rawFileListView;
  QListView* vgmFileListView;
  QListView* vgmCollListView;
  QListView* collListView;
  HeaderContainer* vgmFileListContainer;
  HeaderContainer* rawFileListContainer;

  void dragEnterEvent(QDragEnterEvent *event);
  void dragMoveEvent(QDragMoveEvent* event);
  void dropEvent(QDropEvent *event);
};

#endif // MAINWINDOW_H


