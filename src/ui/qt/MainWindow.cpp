/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QSplitter>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "workarea/RawFileListView.h"
#include "workarea/VGMFileListView.h"
#include "workarea/VGMCollListView.h"
#include "workarea/MdiArea.h"

const int defaultWindowWidth = 800;
const int defaultWindowHeight = 600;
const int defaultCollListHeight = 140;
const int defaultFileListWidth = 200;
const int splitterHandleWidth = 1;

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setWindowIcon(QIcon(":/appicon.png"));

  setUnifiedTitleAndToolBarOnMac(true);
  setAcceptDrops(true);
  setContextMenuPolicy(Qt::NoContextMenu);

  rawFileListView = new RawFileListView();
  vgmFileListView = new VGMFileListView();
  vgmCollListView = new VGMCollListView();

  vertSplitter = new QSplitter(Qt::Vertical, this);
  horzSplitter = new QSplitter(Qt::Horizontal, vertSplitter);
  vertSplitterLeft = new QSplitter(Qt::Vertical, horzSplitter);

  QList<int> sizes({defaultWindowHeight - defaultCollListHeight, defaultCollListHeight});
  vertSplitter->addWidget(horzSplitter);
  vertSplitter->addWidget(vgmCollListView);
  vertSplitter->setStretchFactor(0, 1);
  vertSplitter->setSizes(sizes);
  vertSplitter->setHandleWidth(splitterHandleWidth);

  sizes = QList<int>({defaultFileListWidth, defaultWindowWidth - defaultFileListWidth});
  horzSplitter->addWidget(vertSplitterLeft);
  horzSplitter->addWidget(MdiArea::getInstance());
  horzSplitter->setStretchFactor(1, 1);
  horzSplitter->setSizes(sizes);
  horzSplitter->setHandleWidth(splitterHandleWidth);
  horzSplitter->setMinimumSize(100, 100);
  horzSplitter->setMaximumSize(500, 0);
  horzSplitter->setCollapsible(0, false);
  horzSplitter->setCollapsible(1, false);

  vertSplitterLeft->addWidget(rawFileListView);
  vertSplitterLeft->addWidget(vgmFileListView);
  vertSplitterLeft->setHandleWidth(splitterHandleWidth);

  setCentralWidget(vertSplitter);
  resize(defaultWindowWidth, defaultWindowHeight);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
  event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const auto &files = event->mimeData()->urls();

    if (files.isEmpty())
        return;

    for (const auto &file : files) {
        qtVGMRoot.OpenRawFile(QFileInfo(file.toLocalFile()).filePath().toStdWString());
    }

    setBackgroundRole(QPalette::Dark);
    event->acceptProposedAction();
}
