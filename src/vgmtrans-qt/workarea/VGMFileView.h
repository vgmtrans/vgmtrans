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

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

class QHexView;
class QBuffer;
class QTreeWidgetItem;

class VGMFileTreeView;

class VGMFileView : public QMdiSubWindow {
    Q_OBJECT

   public:
    explicit VGMFileView(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>);

   private:
    void closeEvent(QCloseEvent *closeEvent) override;
    void markEvents();

    QSplitter *m_splitter;
    QHexView *m_hexview;
    VGMFileTreeView *m_treeview;
    std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> m_vgmfile;
    QBuffer *m_buffer;
};
