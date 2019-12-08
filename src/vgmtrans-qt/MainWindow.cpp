/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QFileInfo>
#include <QMimeData>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QFileDialog>
#include <QGridLayout>

#include "QtVGMRoot.h"
#include "MainWindow.h"
#include "MusicPlayer.h"
#include "util/HeaderContainer.h"

#include "About.h"
#include "VGMFile.h"

MainWindow::MainWindow() : QMainWindow(nullptr) {
    setWindowTitle("VGMTrans");
    setWindowIcon(QIcon(":/images/logo.png"));

    setUnifiedTitleAndToolBarOnMac(true);
    setAcceptDrops(true);
    setContextMenuPolicy(Qt::NoContextMenu);

    CreateElements();
    RouteSignals();

    m_statusbar->showMessage("Ready", 3000);
}

void MainWindow::CreateElements() {
    m_menu_bar = new MenuBar(this);
    setMenuBar(m_menu_bar);
    m_iconbar = new IconBar(this);
    addToolBar(m_iconbar);
    m_mdiarea = MdiArea::Init();

    m_statusbar = new QStatusBar(this);
    m_statusbar_offset = new QLabel();
    m_statusbar_length = new QLabel();
    m_statusbar->addPermanentWidget(m_statusbar_offset);
    m_statusbar->addPermanentWidget(m_statusbar_length);
    setStatusBar(m_statusbar);

    m_rawfiles_list = new RawFileListView();
    HeaderContainer *m_rawfiles_list_container =
        new HeaderContainer(m_rawfiles_list, QStringLiteral("Imported Files"));

    m_vgmfiles_list = new VGMFilesList();
    HeaderContainer *m_vgmfiles_list_container =
        new HeaderContainer(m_vgmfiles_list, QStringLiteral("Detected files"));

    m_colls_list = new VGMCollListView();
    HeaderContainer *m_colls_list_container =
        new HeaderContainer(m_colls_list, QStringLiteral("Detected collections"));

    m_coll_view = new VGMCollView(m_colls_list->selectionModel());
    HeaderContainer *m_coll_view_container =
        new HeaderContainer(m_coll_view, QStringLiteral("Selected collection"));

    auto coll_wrapper = new QWidget();
    auto coll_layout = new QGridLayout();
    coll_layout->addWidget(m_colls_list_container, 0, 0, 1, 1);
    coll_layout->addWidget(m_coll_view_container, 0, 1, 1, 3);
    coll_wrapper->setLayout(coll_layout);

    m_logger = new Logger();
    LogManager::instance().addSink(m_logger);
    addDockWidget(Qt::LeftDockWidgetArea, m_logger);
    m_logger->hide();

    vertical_splitter = new QSplitter(Qt::Vertical, this);
    horizontal_splitter = new QSplitter(Qt::Horizontal, vertical_splitter);
    vertical_splitter_left = new QSplitter(Qt::Vertical, horizontal_splitter);

    vertical_splitter->addWidget(coll_wrapper);
    vertical_splitter->setHandleWidth(1);

    horizontal_splitter->addWidget(m_mdiarea);
    horizontal_splitter->setHandleWidth(1);

    vertical_splitter_left->addWidget(m_rawfiles_list_container);
    vertical_splitter_left->addWidget(m_vgmfiles_list_container);
    vertical_splitter_left->setHandleWidth(1);

    setCentralWidget(vertical_splitter);
}

void MainWindow::RouteSignals() {
    connect(m_menu_bar, &MenuBar::OpenFile, this, &MainWindow::OpenFile);
    connect(m_menu_bar, &MenuBar::Exit, this, &MainWindow::close);
    connect(m_menu_bar, &MenuBar::ShowAbout, [=]() {
        About about(this);
        about.exec();
    });

    connect(m_iconbar, &IconBar::OpenPressed, this, &MainWindow::OpenFile);
    connect(m_iconbar, &IconBar::PlayToggle, m_colls_list, &VGMCollListView::HandlePlaybackRequest);
    connect(m_iconbar, &IconBar::StopPressed, m_colls_list, &VGMCollListView::HandleStopRequest);

    connect(&MusicPlayer::Instance(), &MusicPlayer::StatusChange, m_iconbar,
            &IconBar::OnPlayerStatusChange);

    connect(m_menu_bar, &MenuBar::LoggerToggled,
            [=] { m_logger->setHidden(!m_menu_bar->IsLoggerToggled()); });
    connect(m_logger, &Logger::closeEvent, [=] { m_menu_bar->SetLoggerHidden(); });

    connect(m_vgmfiles_list, &VGMCollListView::clicked, [=] {
        if (!m_vgmfiles_list->currentIndex().isValid())
            return;

        VGMFile *clicked_item = qtVGMRoot.vVGMFile[m_vgmfiles_list->currentIndex().row()];
        m_statusbar_offset->setText("Offset: 0x" +
                                    QString::number(clicked_item->dwOffset, 16).toUpper());
        m_statusbar_length->setText("Length: 0x" +
                                    QString::number(clicked_item->size(), 16).toUpper());
    });
}

void MainWindow::OpenFile() {
    auto filenames = QFileDialog::getOpenFileNames(
        this, "Select a file...", QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        "All files (*)");

    if (filenames.isEmpty())
        return;

    for (QString &filename : filenames)
        qtVGMRoot.OpenRawFile(filename.toStdWString());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const auto &files = event->mimeData()->urls();

    if (files.isEmpty())
        return;

    for (const auto &file : files) {
        // Leave sanity checks to the backend
        qtVGMRoot.OpenRawFile(QFileInfo(file.toLocalFile()).filePath().toStdWString());
    }
}
