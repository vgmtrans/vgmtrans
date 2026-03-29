/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDockWidget>
#include <QGridLayout>
#include <QTimer>
#include <QVBoxLayout>
#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariantMap>
#endif
#include <QShortcut>
#include <QMessageBox>
#include <QStatusBar>
#include <QResizeEvent>
#include <QApplication>
#include <QKeyEvent>
#include <filesystem>
#include <version.h>
#include <QWKWidgets/widgetwindowagent.h>
#include "ManualCollectionDialog.h"
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "PlaybackControls.h"
#include "About.h"
#include "Logger.h"
#include "SequencePlayer.h"
#include "services/NotificationCenter.h"
#include "services/Settings.h"
#include "workarea/RawFileListView.h"
#include "workarea/VGMFileListView.h"
#include "workarea/VGMCollListView.h"
#include "workarea/VGMCollView.h"
#include "workarea/hexview/HexViewInput.h"
#include "workarea/MdiArea.h"
#include "TitleBar.h"
#include "StatusBarContent.h"
#include "LogManager.h"
#include "widgets/WindowBar.h"
#include "widgets/ToastHost.h"

namespace {
constexpr auto MIME_PORTAL_FILETRANSFER = "application/vnd.portal.filetransfer";

QStringList retrievePortalDroppedFiles([[maybe_unused]] const QMimeData* mimeData) {
#if defined(VGMTRANS_HAVE_DBUS) && defined(Q_OS_LINUX)
  if (!mimeData) {
    return {};
  }

  QString portalMimeFormat;
  for (const QString& format : mimeData->formats()) {
    if (format == MIME_PORTAL_FILETRANSFER || format.startsWith(MIME_PORTAL_FILETRANSFER)) {
      portalMimeFormat = format;
      break;
    }
  }
  if (portalMimeFormat.isEmpty()) {
    return {};
  }

  QByteArray keyBytes = mimeData->data(portalMimeFormat);
  if (const qsizetype nulPos = keyBytes.indexOf('\0'); nulPos >= 0) {
    keyBytes.truncate(nulPos);
  }

  const QString key = QString::fromUtf8(keyBytes.trimmed());
  if (key.isEmpty()) {
    return {};
  }

  QDBusMessage msg = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.portal.Documents"),
      QStringLiteral("/org/freedesktop/portal/documents"),
      QStringLiteral("org.freedesktop.portal.FileTransfer"),
      QStringLiteral("RetrieveFiles"));
  QVariantMap options;
  msg << key << options;

  QDBusReply<QStringList> reply = QDBusConnection::sessionBus().call(msg);
  if (!reply.isValid()) {
    return {};
  }

  return reply.value();
#else
  return {};
#endif
}
}  // namespace

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setWindowIcon(QIcon(":/vgmtrans.png"));
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
  setAcceptDrops(true);
  setContextMenuPolicy(Qt::NoContextMenu);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);

  m_windowAgent = new QWK::WidgetWindowAgent(this);
  m_windowAgent->setup(this);

  createElements();
  configureWindowAgent();
  routeSignals();
  qApp->installEventFilter(this);

  m_dragOverlay = new QWidget(this);
  m_dragOverlay->setObjectName(QStringLiteral("dragOverlay"));
  m_dragOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dragOverlay->setAcceptDrops(false);
  m_dragOverlay->hide();
  updateDragOverlayAppearance();
  updateDragOverlayGeometry();

  auto infostring = QString("Running %1 (%4, %5), BASS %2, Qt %3")
                        .arg(VGMTRANS_VERSION,
                             QString::number(BASS_GetVersion(), 16),
                             qVersion(),
                             VGMTRANS_REVISION,
                             VGMTRANS_BRANCH)
                        .toStdString();
  L_INFO(infostring);
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);

  const auto installTitleBar = [this](QDockWidget *dock, const QString& title,
                                      TitleBar::Buttons buttons,
                                      const QString& newToolTip = QString()) {
    auto *titleBar = new TitleBar(title, buttons, dock, newToolTip);
    connect(titleBar, &TitleBar::hideRequested, dock, &QDockWidget::hide);
    dock->setTitleBarWidget(titleBar);
    return titleBar;
  };

  m_rawfile_dock = new QDockWidget("Raw files");
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files", TitleBar::HideButton);

  m_vgmfile_dock = new QDockWidget("Detected Music Files");
  m_vgmfile_dock->setWidget(new VGMFileListView());
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_vgmfile_dock, "Detected Music Files", TitleBar::HideButton);


  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  setCentralWidget(MdiArea::the());

  m_coll_listview = new VGMCollListView();
  m_coll_view = new VGMCollView();
  m_playback_controls = new PlaybackControls();

  auto coll_list_area = new QWidget();
  auto coll_list_area_layout = new QVBoxLayout();
  coll_list_area_layout->setContentsMargins(0, 0, 0, 0);
  coll_list_area_layout->addWidget(m_coll_listview);
  coll_list_area->setLayout(coll_list_area_layout);

  auto coll_wrapper = new QWidget();
  auto coll_layout = new QGridLayout();
  coll_layout->addWidget(m_coll_view, 0, 0, 1, 1, Qt::AlignLeft);
  coll_layout->addWidget(coll_list_area, 0, 1, -1, -1);
  coll_wrapper->setLayout(coll_layout);

  m_coll_dock = new QDockWidget("Collections");
  m_coll_dock->setWidget(coll_wrapper);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  TitleBar *collTitleBar = installTitleBar(
      m_coll_dock, "Collections", TitleBar::HideButton | TitleBar::NewButton,
      QStringLiteral("New collection"));
  connect(collTitleBar, &TitleBar::newRequested, this, [this]() {
    ManualCollectionDialog dialog(this);
    dialog.exec();
  });

  m_logger = new Logger();
  m_logger->setWindowTitle("Logs");
  m_logger->setContentsMargins(0, 0, 0, 0);
  addDockWidget(Qt::BottomDockWidgetArea, m_logger);
  TitleBar *loggerTitleBar = installTitleBar(m_logger, "Logs", TitleBar::HideButton);
  m_logger->installTitleBarControls(loggerTitleBar);

  tabifyDockWidget(m_logger, m_coll_dock);
  m_coll_dock->setFocus();

  const QList<QDockWidget *> viewMenuDocks{
      m_vgmfile_dock,
      m_coll_dock,
      m_rawfile_dock,
      m_logger,
  };

  m_windowBar = new WindowBar(this);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(true);
#else
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(false);
  m_windowBar->setMenuBarWidget(m_menu_bar);
#endif
  m_menu_bar->setShortcutHost(this);
  m_windowBar->setCenterWidget(m_playback_controls);
  m_windowBar->setDockToggleButtons({
      {m_vgmfile_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-outline.svg")},
      {m_coll_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-multiple-outline.svg")},
      {m_rawfile_dock->toggleViewAction(), QStringLiteral(":/icons/file-search-outline.svg")},
      {m_logger->toggleViewAction(), QStringLiteral(":/icons/book-open-variant-outline.svg")},
  });
  createStatusBar();
  m_toastHost = new ToastHost(this);
}

void MainWindow::configureWindowAgent() {
  m_windowAgent->setTitleBar(m_windowBar);
  m_windowAgent->setHitTestVisible(m_windowBar->dockControls(), true);
  if (QWidget *menuBarWidget = m_windowBar->menuBarWidget()) {
    m_windowAgent->setHitTestVisible(menuBarWidget, true);
  }
  if (QWidget *centerWidget = m_windowBar->centerWidget()) {
    for (QWidget *child : centerWidget->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
      m_windowAgent->setHitTestVisible(child, true);
    }
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_windowAgent->setWindowAttribute(QStringLiteral("no-system-buttons"), false);
  setMenuWidget(m_windowBar);
  m_windowAgent->setSystemButtonArea(m_windowBar->systemButtonArea());
#else
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::WindowIcon, m_windowBar->windowIconButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, m_windowBar->minimizeButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, m_windowBar->maximizeButton());
  m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, m_windowBar->closeButton());
  setMenuWidget(m_windowBar);
#endif
}

void MainWindow::createStatusBar() {
  statusBarContent = new StatusBarContent;
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setMaximumHeight(statusBarContent->maximumHeight());
  statusBar()->addPermanentWidget(statusBarContent, 1);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);

  // Set the initial heights of the docks in relation to the main window height
  QList<int> sizes;
  int totalHeight = this->height();
  // Calculate the desired heights for the dock widgets
  sizes << totalHeight * 3 / 10;   // Raw Files
  sizes << totalHeight * 7 / 10;   // VGM Files
  sizes << totalHeight / 4;        // Collections

  resizeDocks({m_rawfile_dock, m_vgmfile_dock, m_coll_dock}, sizes, Qt::Vertical);

  updateDragOverlayGeometry();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QTimer::singleShot(0, this, [this]() {
    m_windowAgent->setSystemButtonArea(nullptr);
    m_windowAgent->setSystemButtonArea(m_windowBar->systemButtonArea());
  });
#endif
}

void MainWindow::routeSignals() {
  connect(m_menu_bar, &MenuBar::openFile, this, &MainWindow::openFile);
  connect(m_menu_bar, &MenuBar::openRecentFile, this, &MainWindow::openFileInternal);
  connect(m_menu_bar, &MenuBar::exit, this, &MainWindow::close);
  connect(m_menu_bar, &MenuBar::showAbout, [this]() {
    About about(this);
    about.exec();
  });

  connect(m_playback_controls, &PlaybackControls::playToggle, m_coll_listview,
          &VGMCollListView::handlePlaybackRequest);
  connect(m_coll_listview, &VGMCollListView::nothingToPlay, m_playback_controls,
          &PlaybackControls::showPlayInfo);
  connect(m_playback_controls, &PlaybackControls::stopPressed, m_coll_listview,
          &VGMCollListView::handleStopRequest);
  connect(m_playback_controls, &PlaybackControls::seekingTo, &SequencePlayer::the(),
          &SequencePlayer::seek);
  connect(&qtVGMRoot, &QtVGMRoot::UI_toastRequested, this, &MainWindow::showToast);

  auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, m_coll_listview, &VGMCollListView::handlePlaybackRequest);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  Q_UNUSED(obj);
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!keyEvent->isAutoRepeat() && keyEvent->key() == HexViewInput::kModifierKey) {
      const bool active = event->type() == QEvent::KeyPress;
      NotificationCenter::the()->setSeekModifierActive(active);
    }
  } else if (event->type() == QEvent::ApplicationDeactivate) {
    NotificationCenter::the()->setSeekModifierActive(false);
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event) {
  event->accept();
  hideDragOverlay();
}

void MainWindow::dropEvent(QDropEvent *event) {
  hideDragOverlay();

  const QMimeData* mimeData = event->mimeData();
  const QStringList portalFiles = retrievePortalDroppedFiles(mimeData);
  if (!portalFiles.isEmpty()) {
    for (const QString& filePath : portalFiles) {
      if (!filePath.isEmpty()) {
        openFileInternal(filePath);
      }
    }
    event->acceptProposedAction();
    return;
  }

  handleDroppedUrls(mimeData->urls());
  event->acceptProposedAction();
}

void MainWindow::showDragOverlay() {
  updateDragOverlayGeometry();
  if (!m_dragOverlay->isVisible()) {
    m_dragOverlay->show();
  }
  m_dragOverlay->raise();
}

void MainWindow::hideDragOverlay() {
  m_dragOverlay->hide();
}

void MainWindow::handleDroppedUrls(const QList<QUrl>& urls) {
  hideDragOverlay();

  if (urls.isEmpty()) {
    return;
  }

  for (const auto &url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }

    const QString localFile = url.toLocalFile();
    if (!localFile.isEmpty()) {
      openFileInternal(localFile);
    }
  }
}

void MainWindow::openFile() {
  auto filenames = QFileDialog::getOpenFileNames(
      this, "Select a file...", QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      "All files (*)");

  if (filenames.isEmpty())
    return;

  for (QString &filename : filenames) {
    openFileInternal(filename);
  }
}

void MainWindow::openFileInternal(const QString& filename) {
  static QString UNSUPPORTED_RAW_IMAGE_WARNING{
      "'%1' is a raw image file. Data is unlikely to be read correctly, do you wish "
      "to continue anyway?"};

  static QString UNSUPPORTED_RAW_IMAGE_DESCRIPTION{
      "If this is a dump of a CD or DVD (e.g. PlayStation), please "
      "convert it to '.iso'. The program cannot read raw dumps from optical media."};

  auto file_info = QFileInfo(filename);
  if (file_info.completeSuffix().contains("img")) {
    QMessageBox user_choice(QMessageBox::Icon::Warning, "File format might be unsopported",
                            UNSUPPORTED_RAW_IMAGE_WARNING.arg(file_info.fileName()),
                            QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No,
                            this);
    user_choice.setInformativeText(UNSUPPORTED_RAW_IMAGE_DESCRIPTION);
    user_choice.setWindowModality(Qt::WindowModal);
    user_choice.exec();

    if (user_choice.result() != QMessageBox::StandardButton::Yes) {
      return;
    }
  }

  if (qtVGMRoot.openRawFile(filename.toStdWString())) {
    Settings::the()->recentFiles.add(filename);
    m_menu_bar->updateRecentFilesMenu();
  }
}

void MainWindow::showToast(const QString& message, ToastType type, int duration_ms) {
  m_toastHost->showToast(message, type, duration_ms);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  updateDragOverlayGeometry();
}

void MainWindow::updateDragOverlayAppearance() {
  m_dragOverlay->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 102);"));
}

void MainWindow::updateDragOverlayGeometry() {
  m_dragOverlay->setGeometry(rect());
  m_dragOverlay->raise();
}
