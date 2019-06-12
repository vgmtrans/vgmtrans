/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QSplitter>
#include <QMdiSubWindow>
#include <QCloseEvent>
#include <QGridLayout>

class VGMFile;

class QHexView;
class QBuffer;
class QTreeWidgetItem;

class VGMFileTreeView;

class VGMFileView : public QMdiSubWindow {
    Q_OBJECT

   public:
    explicit VGMFileView(VGMFile *vgmfile);

   private:
    void closeEvent(QCloseEvent *closeEvent) override;
    void markEvents();
    void highlightItem(QTreeWidgetItem *item, int col);

    QSplitter *m_splitter;
    QHexView *m_hexview;
    VGMFileTreeView *m_treeview;
    VGMFile *m_vgmfile;
    QBuffer *m_buffer;
};
