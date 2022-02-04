/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QSplitter>
#include <QFileDialog>
#include <QStandardPaths>
#include <QGridLayout>
#include <QPushButton>
#include <QMessageBox>
#include <version.h>
#include "ManualCollectionDialog.h"
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "IconBar.h"
#include "About.h"
#include "Logger.h"
#include "SequencePlayer.h"
#include "workarea/RawFileListView.h"
#include "workarea/VGMFileListView.h"
#include "workarea/VGMCollListView.h"
#include "workarea/VGMCollView.h"
#include "workarea/MdiArea.h"

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setWindowIcon(QIcon(":/vgmtrans.png"));

  setUnifiedTitleAndToolBarOnMac(true);
  setAcceptDrops(true);
  setContextMenuPolicy(Qt::NoContextMenu);

  createElements();
  routeSignals();

  auto infostring = QString("Running %1 (%4, %5), BASS %2, Qt %3")
                        .arg(VGMTRANS_VERSION)
                        .arg(int(BASS_GetVersion()), 0, 16)
                        .arg(qVersion())
                        .arg(VGMTRANS_REVISION)
                        .arg(VGMTRANS_BRANCH)
                        .toStdWString();
  qtVGMRoot.UI_AddLogItem(new LogItem(infostring, LOG_LEVEL_INFO, L"VGMTransQt"));
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);

  m_rawfile_dock = new QDockWidget("Raw files");
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  m_rawfile_dock->setTitleBarWidget(new QWidget());

  m_vgmfile_dock = new QDockWidget("Detected music files");
  m_vgmfile_dock->setWidget(new VGMFileListView());
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  m_vgmfile_dock->setTitleBarWidget(new QWidget());

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  tabifyDockWidget(m_rawfile_dock, m_vgmfile_dock);
  m_vgmfile_dock->setFocus();

  setCentralWidget(MdiArea::the());

  m_coll_listview = new VGMCollListView();
  m_coll_view = new VGMCollView(m_coll_listview->selectionModel());
  m_icon_bar = new IconBar();

  auto coll_list_area = new QWidget();
  auto coll_list_area_layout = new QVBoxLayout();
  coll_list_area_layout->setContentsMargins(0, 0, 0, 0);
  coll_list_area_layout->addWidget(m_coll_listview);
  coll_list_area_layout->addWidget(m_icon_bar);
  coll_list_area->setLayout(coll_list_area_layout);

  auto coll_wrapper = new QWidget();
  auto coll_layout = new QGridLayout();
  coll_layout->addWidget(m_coll_view, 0, 0, 1, 1, Qt::AlignLeft);
  coll_layout->addWidget(coll_list_area, 0, 1, -1, -1);
  coll_wrapper->setLayout(coll_layout);

  auto coll_widget = new QDockWidget("Collections");
  coll_widget->setWidget(coll_wrapper);
  coll_widget->setContentsMargins(0, 0, 0, 0);
  addDockWidget(Qt::BottomDockWidgetArea, coll_widget);
  coll_widget->setTitleBarWidget(new QWidget());

  m_logger = new Logger();
  addDockWidget(Qt::BottomDockWidgetArea, m_logger);
  m_logger->setTitleBarWidget(new QWidget());

  tabifyDockWidget(m_logger, coll_widget);
  coll_widget->setFocus();

  QList<QDockWidget *> docks = findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
  m_menu_bar = new MenuBar(this, docks);
  setMenuBar(m_menu_bar);
}

void MainWindow::routeSignals() {
  connect(m_menu_bar, &MenuBar::openFile, this, &MainWindow::OpenFile);
  connect(m_menu_bar, &MenuBar::exit, this, &MainWindow::close);
  connect(m_menu_bar, &MenuBar::showAbout, [=]() {
    About about(this);
    about.exec();
  });

  connect(m_icon_bar, &IconBar::playToggle, m_coll_listview,
          &VGMCollListView::handlePlaybackRequest);
  connect(m_coll_listview, &VGMCollListView::nothingToPlay, m_icon_bar, &IconBar::showPlayInfo);
  connect(m_icon_bar, &IconBar::stopPressed, m_coll_listview, &VGMCollListView::handleStopRequest);
  connect(m_icon_bar, &IconBar::seekingTo, &SequencePlayer::the(), &SequencePlayer::seek);
  connect(m_icon_bar, &IconBar::createPressed, [=]() {
    ManualCollectionDialog wiz(this);
    wiz.exec();
  });
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
    openFileInternal(file.toLocalFile());
  }

  setBackgroundRole(QPalette::Dark);
  event->acceptProposedAction();
}

void MainWindow::OpenFile() {
  auto filenames = QFileDialog::getOpenFileNames(
      this, "Select a file...", QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      "All files (*)");

  if (filenames.isEmpty())
    return;

  for (QString &filename : filenames) {
    openFileInternal(filename);
  }
}

void MainWindow::openFileInternal(QString filename) {
  static QString UNSUPPORTED_RAW_IMAGE_WARNING{
      "VGMTrans has detected a .img file in input. Please note that raw "
      "optical media images are not "
      "supported.\n\n"
      "If this is a dump of a CD or DVD (e.g. PlayStation), please "
      "convert it to .iso: the program is otherwise "
      "unable to read the file contents correctly.\n\n"
      "Do you wish to try and load the file anyway?"};

  auto file_info = QFileInfo(filename);
  if (file_info.completeSuffix().contains("img")) {
    QMessageBox user_choice(QMessageBox::Icon::Warning,
        "File format might be unsopported", UNSUPPORTED_RAW_IMAGE_WARNING,
        QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No, this);
    user_choice.setWindowModality(Qt::WindowModal);
    user_choice.exec();

    if (user_choice.result() != QMessageBox::StandardButton::Yes) {
      return;
    }
  }

  qtVGMRoot.OpenRawFile(filename.toStdWString());
}
