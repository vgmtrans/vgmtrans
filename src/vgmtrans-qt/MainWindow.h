/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QStatusBar>
#include <QLabel>

#include "MenuBar.h"
#include "workarea/RawFileListView.h"
#include "workarea/VGMFilesList.h"
#include "workarea/VGMCollView.h"
#include "workarea/VGMCollListView.h"
#include "workarea/MdiArea.h"
#include "IconBar.h"
#include "Logger.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit MainWindow();

   private:
    void CreateElements();
    void RouteSignals();

    void OpenFile();

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    MenuBar *m_menu_bar;
    RawFileListView *m_rawfiles_list;
    VGMFilesList *m_vgmfiles_list;
    VGMCollListView *m_colls_list;
    VGMCollView *m_coll_view;
    IconBar *m_iconbar;
    Logger *m_logger;
    MdiArea *m_mdiarea;

    QStatusBar *m_statusbar;
    QLabel *m_statusbar_offset;
    QLabel *m_statusbar_length;

    QSplitter *vertical_splitter;
    QSplitter *horizontal_splitter;
    QSplitter *vertical_splitter_left;
};
