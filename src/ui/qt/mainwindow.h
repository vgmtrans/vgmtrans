#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <qsplitter.h>
#include <qtreeview.h>
#include <qlistview.h>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    QSplitter *vertSplitter;
    QSplitter *horzSplitter;
    QSplitter *vertSplitterLeft;
    QListView *rawFileListView;
    QListView *vgmFileListView;
    QListView *vgmCollListView;
    QListView *collListView;

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent* event);
    void dropEvent(QDropEvent *event);
};

#endif // MAINWINDOW_H


