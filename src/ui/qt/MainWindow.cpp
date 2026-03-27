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
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <algorithm>
#include <filesystem>
#include <utility>
#include <version.h>
#include <QWKWidgets/widgetwindowagent.h>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "PlaybackControls.h"
#include "About.h"
#include "widgets/ItemViewDensity.h"
#include "Logger.h"
#include "ManualCollectionDialog.h"
#include "SequencePlayer.h"
#include "services/NotificationCenter.h"
#include "services/Settings.h"
#include "util/UIHelpers.h"
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
constexpr int kDockLayoutStateVersion = 2;

bool isDockSeparatorCursor(Qt::CursorShape shape) {
  return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
}

bool isVisibleDockInArea(const QMainWindow *window, QDockWidget *dock, Qt::DockWidgetArea area) {
  return dock && dock->isVisible() && !dock->isFloating() && window->dockWidgetArea(dock) == area;
}

bool hasVisibleDockInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                          Qt::DockWidgetArea area) {
  for (QDockWidget *dock : docks) {
    if (isVisibleDockInArea(window, dock, area)) {
      return true;
    }
  }
  return false;
}

QDockWidget *firstVisibleDockInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                                    Qt::DockWidgetArea area) {
  for (QDockWidget *dock : docks) {
    if (isVisibleDockInArea(window, dock, area)) {
      return dock;
    }
  }
  return nullptr;
}

int dockSizeForOrientation(QDockWidget *dock, Qt::Orientation orientation) {
  return orientation == Qt::Horizontal ? dock->width() : dock->height();
}

int firstVisibleDockSizeInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                               Qt::DockWidgetArea area, Qt::Orientation orientation) {
  if (QDockWidget *dock = firstVisibleDockInArea(window, docks, area)) {
    return dockSizeForOrientation(dock, orientation);
  }
  return 0;
}

bool isLeftMostDockInArea(const QMainWindow *window, QDockWidget *dock, std::initializer_list<QDockWidget *> docks,
                          Qt::DockWidgetArea area) {
  if (!isVisibleDockInArea(window, dock, area)) {
    return false;
  }

  int leftMostX = dock->geometry().left();
  for (QDockWidget *candidate : docks) {
    if (!isVisibleDockInArea(window, candidate, area)) {
      continue;
    }
    leftMostX = std::min(leftMostX, candidate->geometry().left());
  }

  return dock->geometry().left() == leftMostX;
}

QDockWidget *leftMostDockInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                                Qt::DockWidgetArea area) {
  QDockWidget *leftMostDock = nullptr;
  for (QDockWidget *dock : docks) {
    if (!isVisibleDockInArea(window, dock, area)) {
      continue;
    }
    if (!leftMostDock || dock->geometry().left() < leftMostDock->geometry().left()) {
      leftMostDock = dock;
    }
  }
  return leftMostDock;
}

QDockWidget *bottomMostDockInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                                  Qt::DockWidgetArea area) {
  QDockWidget *bottomMostDock = nullptr;
  for (QDockWidget *dock : docks) {
    if (!isVisibleDockInArea(window, dock, area)) {
      continue;
    }
    if (!bottomMostDock || dock->geometry().bottom() > bottomMostDock->geometry().bottom()) {
      bottomMostDock = dock;
    }
  }
  return bottomMostDock;
}

QList<QDockWidget *> visibleDocksInAreaSorted(const QMainWindow *window,
                                              std::initializer_list<QDockWidget *> docks,
                                              Qt::DockWidgetArea area,
                                              Qt::Orientation orientation) {
  QList<QDockWidget *> visibleDocks;
  for (QDockWidget *dock : docks) {
    if (isVisibleDockInArea(window, dock, area)) {
      visibleDocks.append(dock);
    }
  }

  std::sort(visibleDocks.begin(), visibleDocks.end(), [orientation](QDockWidget *lhs, QDockWidget *rhs) {
    if (orientation == Qt::Horizontal) {
      return lhs->geometry().left() < rhs->geometry().left();
    }
    return lhs->geometry().top() < rhs->geometry().top();
  });

  return visibleDocks;
}

template <typename Fn>
void runWithUpdatesSuspended(QWidget *widget, Fn &&fn) {
  const bool updatesWereEnabled = widget->updatesEnabled();
  if (updatesWereEnabled) {
    widget->setUpdatesEnabled(false);
  }

  std::forward<Fn>(fn)();

  if (updatesWereEnabled) {
    widget->setUpdatesEnabled(true);
    widget->update();
  }
}

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

  if (const QByteArray geometry = Settings::the()->mainWindow.windowGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
  }
  m_savedDockState = Settings::the()->mainWindow.dockState();

  auto infostring = QString("Running %1 (%4, %5), BASS %2, Qt %3")
                        .arg(VGMTRANS_VERSION,
                             QString::number(BASS_GetVersion(), 16),
                             qVersion(),
                             VGMTRANS_REVISION,
                             VGMTRANS_BRANCH)
                        .toStdString();
  L_INFO(infostring);
}

void MainWindow::activateMainLayout() {
  if (QLayout *mainLayout = layout()) {
    mainLayout->activate();
  }
}

void MainWindow::captureLeftDockAreaWidth() {
  if (const int dockSize = firstVisibleDockSizeInArea(
          this,
          {m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
          Qt::LeftDockWidgetArea,
          Qt::Horizontal);
      dockSize > 0) {
    m_leftDockAreaPreferredWidth = dockSize;
  }
}

void MainWindow::captureBottomDockAreaHeight() {
  if (const int dockSize = firstVisibleDockSizeInArea(
          this,
          {m_coll_dock, m_coll_view_dock, m_logger},
          Qt::BottomDockWidgetArea,
          Qt::Vertical);
      dockSize > 0) {
    m_bottomDockAreaPreferredHeight = dockSize;
  }
}

void MainWindow::captureCollectionContentsLeftDockHeight() {
  if (m_defaultDockState.isEmpty() || m_adjustingDockLayout || m_restoringDockState || m_closingDown) {
    return;
  }

  if (isVisibleDockInArea(this, m_coll_view_dock, Qt::LeftDockWidgetArea) &&
      bottomMostDockInArea(this,
                           {m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
                           Qt::LeftDockWidgetArea) == m_coll_view_dock &&
      !hasVisibleDockInArea(this, {m_coll_dock, m_logger}, Qt::BottomDockWidgetArea)) {
    m_collectionContentsLeftDockHeight = m_coll_view_dock->height();
    m_bottomDockAreaPreferredHeight = m_collectionContentsLeftDockHeight;
    m_pendingCollectionContentsBottomHeight = m_collectionContentsLeftDockHeight;
  }
}

void MainWindow::applyPendingCollectionContentsBottomAreaHeight() {
  if (m_restoringDockState || m_closingDown || m_pendingCollectionContentsBottomHeight <= 0 ||
      !isVisibleDockInArea(this, m_coll_view_dock, Qt::BottomDockWidgetArea)) {
    return;
  }

  m_bottomDockAreaPreferredHeight = m_pendingCollectionContentsBottomHeight;
  applyDockAreaTargets(false, true);
  activateMainLayout();
  m_pendingCollectionContentsBottomHeight = 0;
}

void MainWindow::syncDockLayoutState(bool persistState) {
  activateMainLayout();
  captureLeftDockAreaWidth();
  captureBottomDockAreaHeight();
  updateCollectionContentsWidthLock();
  if (persistState) {
    m_savedDockState = saveState(kDockLayoutStateVersion);
  }
}

void MainWindow::applyDockAreaTargets(bool applyLeftWidth, bool applyBottomHeight) {
  bool resized = false;
  const auto resizeAreaToPreferredSize =
      [this, &resized](std::initializer_list<QDockWidget *> docks, Qt::DockWidgetArea area,
                       Qt::Orientation orientation, int preferredSize) {
        if (preferredSize <= 0) {
          return;
        }

        if (QDockWidget *dock = firstVisibleDockInArea(this, docks, area)) {
          resizeDocks({dock}, {preferredSize}, orientation);
          resized = true;
        }
      };

  if (applyLeftWidth) {
    resizeAreaToPreferredSize({m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
                              Qt::LeftDockWidgetArea,
                              Qt::Horizontal,
                              m_leftDockAreaPreferredWidth);
  }

  if (applyBottomHeight) {
    resizeAreaToPreferredSize({m_coll_dock, m_coll_view_dock, m_logger},
                              Qt::BottomDockWidgetArea,
                              Qt::Vertical,
                              m_bottomDockAreaPreferredHeight);
  }

  if (resized) {
    activateMainLayout();
  }
}

bool MainWindow::moveCollectionContentsToLeftDockIfNeeded() {
  const auto leftAreaDocks =
      std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock};
  const auto allLeftAreaDocks =
      std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock};
  const auto bottomAreaDocks =
      std::initializer_list<QDockWidget *>{m_coll_view_dock, m_coll_dock, m_logger};
  const auto otherBottomAreaDocks =
      std::initializer_list<QDockWidget *>{m_coll_dock, m_logger};

  if (!hasVisibleDockInArea(this, allLeftAreaDocks, Qt::LeftDockWidgetArea) ||
      !isLeftMostDockInArea(this, m_coll_view_dock, bottomAreaDocks, Qt::BottomDockWidgetArea) ||
      hasVisibleDockInArea(this, otherBottomAreaDocks, Qt::BottomDockWidgetArea)) {
    return false;
  }

  QList<QDockWidget *> leftDocks = visibleDocksInAreaSorted(this, leftAreaDocks, Qt::LeftDockWidgetArea,
                                                            Qt::Vertical);
  QDockWidget *anchorDock = leftDocks.isEmpty() ? nullptr : leftDocks.constLast();
  if (!anchorDock) {
    return false;
  }

  const int collViewHeight = m_coll_view_dock->height();
  QList<int> leftDockHeights;
  leftDockHeights.reserve(leftDocks.size() + 1);
  for (QDockWidget *dock : leftDocks) {
    leftDockHeights.append(dock->height());
  }
  leftDocks.append(m_coll_view_dock);
  leftDockHeights.append(collViewHeight);

  m_collectionContentsLeftDockHeight = collViewHeight;
  m_pendingCollectionContentsBottomHeight = 0;
  m_adjustingDockLayout = true;
  m_coll_view_dock->setMinimumWidth(0);
  m_coll_view_dock->setMaximumWidth(QWIDGETSIZE_MAX);
  splitDockWidget(anchorDock, m_coll_view_dock, Qt::Vertical);
  activateMainLayout();
  resizeDocks(leftDocks, leftDockHeights, Qt::Vertical);
  activateMainLayout();
  m_adjustingDockLayout = false;
  return true;
}

bool MainWindow::moveCollectionContentsToBottomDockIfNeeded() {
  const auto leftAreaDocks =
      std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock};
  const auto bottomAreaDocks =
      std::initializer_list<QDockWidget *>{m_coll_view_dock, m_coll_dock, m_logger};
  const auto otherBottomAreaDocks =
      std::initializer_list<QDockWidget *>{m_coll_dock, m_logger};

  if (!isVisibleDockInArea(this, m_coll_view_dock, Qt::LeftDockWidgetArea) ||
      bottomMostDockInArea(this, leftAreaDocks, Qt::LeftDockWidgetArea) != m_coll_view_dock ||
      !hasVisibleDockInArea(this, otherBottomAreaDocks, Qt::BottomDockWidgetArea)) {
    return false;
  }

  QDockWidget *anchorDock = leftMostDockInArea(this, otherBottomAreaDocks, Qt::BottomDockWidgetArea);
  if (!anchorDock) {
    return false;
  }

  const int collViewWidth = m_coll_view_dock->width();
  const int collViewHeight =
      m_collectionContentsLeftDockHeight > 0 ? m_collectionContentsLeftDockHeight : m_coll_view_dock->height();

  m_pendingCollectionContentsBottomHeight = collViewHeight;
  m_adjustingDockLayout = true;
  m_coll_view_dock->setMinimumWidth(0);
  m_coll_view_dock->setMaximumWidth(QWIDGETSIZE_MAX);
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_view_dock);
  splitDockWidget(m_coll_view_dock, anchorDock, Qt::Horizontal);
  activateMainLayout();

  m_bottomDockAreaPreferredHeight = collViewHeight;
  QList<QDockWidget *> bottomDocks;
  QList<int> bottomDockHeights;
  for (QDockWidget *dock : bottomAreaDocks) {
    if (isVisibleDockInArea(this, dock, Qt::BottomDockWidgetArea)) {
      bottomDocks.append(dock);
      bottomDockHeights.append(collViewHeight);
    }
  }
  if (!bottomDocks.isEmpty()) {
    resizeDocks(bottomDocks, bottomDockHeights, Qt::Vertical);
  }
  resizeDocks({m_coll_view_dock}, {collViewWidth}, Qt::Horizontal);
  activateMainLayout();
  m_adjustingDockLayout = false;
  return true;
}

bool MainWindow::normalizeCollectionContentsDockPlacement() {
  if (m_adjustingDockLayout || m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return false;
  }

  return moveCollectionContentsToLeftDockIfNeeded() || moveCollectionContentsToBottomDockIfNeeded();
}

void MainWindow::settleDockLayoutChange(bool applyAreaTargets) {
  if (m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return;
  }

  if (m_adjustingDockLayout) {
    return;
  }

  runWithUpdatesSuspended(this, [this, applyAreaTargets]() {
    activateMainLayout();
    const bool normalizedCollectionContents = normalizeCollectionContentsDockPlacement();
    const bool applyBottomHeight =
        applyAreaTargets ||
        (normalizedCollectionContents && isVisibleDockInArea(this, m_coll_view_dock, Qt::BottomDockWidgetArea));
    if (applyAreaTargets || applyBottomHeight) {
      applyDockAreaTargets(applyAreaTargets, applyBottomHeight);
    }
    updateCollectionContentsWidthLock();
    activateMainLayout();
  });

  scheduleDockStateUpdate();
}

void MainWindow::updateCollectionContentsWidthLock() {
  constexpr int kUnlockedMinimumWidth = 0;
  constexpr int kUnlockedMaximumWidth = QWIDGETSIZE_MAX;

  const bool shouldLockWidth =
      hasVisibleDockInArea(this,
                           {m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
                           Qt::LeftDockWidgetArea) &&
      isLeftMostDockInArea(this,
                           m_coll_view_dock,
                           {m_coll_view_dock, m_coll_dock, m_logger},
                           Qt::BottomDockWidgetArea);

  if (!shouldLockWidth) {
    m_coll_view_dock->setMinimumWidth(kUnlockedMinimumWidth);
    m_coll_view_dock->setMaximumWidth(kUnlockedMaximumWidth);
    return;
  }

  const int targetWidth = firstVisibleDockSizeInArea(
      this,
      {m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
      Qt::LeftDockWidgetArea,
      Qt::Horizontal);
  if (targetWidth <= 0) {
    m_coll_view_dock->setMinimumWidth(kUnlockedMinimumWidth);
    m_coll_view_dock->setMaximumWidth(kUnlockedMaximumWidth);
    return;
  }

  m_coll_view_dock->setMinimumWidth(targetWidth);
  m_coll_view_dock->setMaximumWidth(targetWidth);
}

void MainWindow::scheduleDockStateUpdate() {
  if (m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return;
  }

  // Defer until the current dock/layout change finishes so we capture the settled user layout.
  QTimer::singleShot(0, this, [this]() {
    if (m_adjustingDockLayout || m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
      return;
    }

    runWithUpdatesSuspended(this, [this]() {
      activateMainLayout();
      normalizeCollectionContentsDockPlacement();
      activateMainLayout();
      applyPendingCollectionContentsBottomAreaHeight();
      captureCollectionContentsLeftDockHeight();
      syncDockLayoutState(true);
    });
  });
}

void MainWindow::applyDefaultDockLayout() {
  m_collectionContentsLeftDockHeight = 0;
  m_pendingCollectionContentsBottomHeight = 0;
  m_rawfile_dock->show();
  m_vgmfile_dock->show();
  m_coll_dock->show();
  m_coll_view_dock->show();
  m_logger->show();
  activateMainLayout();

  const int bottomDockAreaHeight = Size::VTab +
      horizontalScrollBarReservedHeight(m_coll_listview) +
      static_cast<int>(4.5 * ItemViewDensity::listItemStride(m_coll_listview));

  resizeDocks({m_rawfile_dock, m_vgmfile_dock}, {26, 74}, Qt::Vertical);
  resizeDocks({m_coll_view_dock, m_coll_dock, m_logger},
              {bottomDockAreaHeight, bottomDockAreaHeight, bottomDockAreaHeight},
              Qt::Vertical);
  resizeDocks({m_coll_view_dock, m_coll_dock, m_logger}, {27, 38, 35}, Qt::Horizontal);
  activateMainLayout();
  m_logger->hide();
}

void MainWindow::showRestoredFloatingDocks() {
  QTimer::singleShot(0, this, [this]() {
    for (QDockWidget *dock : std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock,
                                                                   m_coll_view_dock, m_logger}) {
      if (!dock || !dock->isFloating() || !dock->toggleViewAction()->isChecked()) {
        continue;
      }
      dock->show();
      const QByteArray geometry = Settings::the()->mainWindow.floatingDockGeometry(dock->objectName());
      if (!geometry.isEmpty()) {
        dock->restoreGeometry(geometry);
      }
      dock->raise();
    }
  });
}

void MainWindow::resetDockLayout() {
  if (m_defaultDockState.isEmpty()) {
    return;
  }

  m_dockSeparatorDragActive = false;

  if (!restoreState(m_defaultDockState, kDockLayoutStateVersion)) {
    return;
  }

  applyDefaultDockLayout();
  syncDockLayoutState(true);
  saveLayoutSettings();
}

void MainWindow::saveLayoutSettings() const {
  Settings::the()->mainWindow.setWindowGeometry(saveGeometry());
  for (QDockWidget *dock : std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock,
                                                                 m_coll_view_dock, m_logger}) {
    if (!dock) {
      continue;
    }
    if (dock->isFloating()) {
      Settings::the()->mainWindow.setFloatingDockGeometry(dock->objectName(), dock->saveGeometry());
    }
  }
  if (!m_savedDockState.isEmpty()) {
    Settings::the()->mainWindow.setDockState(m_savedDockState);
  } else {
    Settings::the()->mainWindow.clearDockState();
  }
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  const auto installTitleBar = [this](QDockWidget *dock, const QString& title,
                                      TitleBar::Buttons buttons,
                                      const QString& newToolTip = QString()) {
    auto *titleBar = new TitleBar(title, buttons, dock, newToolTip);
    connect(titleBar, &TitleBar::hideRequested, dock, &QDockWidget::hide);
    dock->setTitleBarWidget(titleBar);
    return titleBar;
  };

  m_rawfile_dock = new QDockWidget("Scanned Files");
  m_rawfile_dock->setObjectName(QStringLiteral("rawFileListDock"));
  m_rawfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files", TitleBar::HideButton);

  m_vgmfile_dock = new QDockWidget("Detected Files");
  m_vgmfile_dock->setObjectName(QStringLiteral("vgmFileListDock"));
  m_vgmfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  auto* vgmfileListView = new VGMFileListView();
  vgmfileListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_vgmfile_dock->setWidget(vgmfileListView);
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_vgmfile_dock, "Detected Files", TitleBar::HideButton);

  m_coll_listview = new VGMCollListView();
  m_coll_view = new VGMCollView();
  m_playback_controls = new PlaybackControls();

  auto *central_wrapper = new QWidget(this);
  auto *central_layout = new QVBoxLayout();
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  central_layout->addWidget(MdiArea::the(), 1);
  central_wrapper->setLayout(central_layout);
  setCentralWidget(central_wrapper);

  m_coll_dock = new QDockWidget("Collections");
  m_coll_dock->setObjectName(QStringLiteral("collectionListDock"));
  m_coll_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_dock->setWidget(m_coll_listview);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  TitleBar *collTitleBar = installTitleBar(
      m_coll_dock, "Collections", TitleBar::HideButton | TitleBar::NewButton,
      QStringLiteral("New collection"));
  connect(collTitleBar, &TitleBar::newRequested, this, [this]() {
    ManualCollectionDialog dialog(this);
    dialog.exec();
  });

  m_coll_view_dock = new QDockWidget("Collection Contents");
  m_coll_view_dock->setObjectName(QStringLiteral("collectionContentDock"));
  m_coll_view_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_view_dock->setWidget(m_coll_view);
  m_coll_view_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_view_dock, "Collection Contents", TitleBar::HideButton);

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  m_logger = new Logger();
  m_logger->setObjectName(QStringLiteral("loggerDock"));
  m_logger->setWindowTitle("Logs");
  m_logger->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_logger->setContentsMargins(0, 0, 0, 0);
  TitleBar *loggerTitleBar = installTitleBar(m_logger, "Logs", TitleBar::HideButton);
  m_logger->installTitleBarControls(loggerTitleBar);

  addDockWidget(Qt::BottomDockWidgetArea, m_coll_view_dock);
  // Keep the bottom docks in a side-by-side layout so each dock preserves its own width.
  splitDockWidget(m_coll_view_dock, m_coll_dock, Qt::Horizontal);
  splitDockWidget(m_coll_dock, m_logger, Qt::Horizontal);

  const QList<QDockWidget *> viewMenuDocks{
      m_vgmfile_dock, m_coll_dock, m_coll_view_dock, m_rawfile_dock, m_logger,
  };

  for (QDockWidget *dock : viewMenuDocks) {
    if (!dock) {
      continue;
    }
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) { settleDockLayoutChange(false); });
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { settleDockLayoutChange(false); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
      if (!floating) {
        QTimer::singleShot(0, this, [this]() { settleDockLayoutChange(true); });
      } else {
        scheduleDockStateUpdate();
      }
    });
  }
  connect(m_coll_dock->toggleViewAction(), &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      captureCollectionContentsLeftDockHeight();
    }
  });
  connect(m_logger->toggleViewAction(), &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      captureCollectionContentsLeftDockHeight();
    }
  });
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
      {m_coll_view_dock->toggleViewAction(), QStringLiteral(":/icons/package-variant.svg")},
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

  if (m_defaultDockState.isEmpty()) {
    m_restoringDockState = true;
    applyDefaultDockLayout();
    m_defaultDockState = saveState(kDockLayoutStateVersion);

    if (!m_savedDockState.isEmpty() &&
        !restoreState(m_savedDockState, kDockLayoutStateVersion)) {
      m_savedDockState.clear();
    }
    showRestoredFloatingDocks();

    if (m_savedDockState.isEmpty()) {
      m_savedDockState = m_defaultDockState;
    }

    m_restoringDockState = false;
    syncDockLayoutState(false);
  }

  updateDragOverlayGeometry();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QTimer::singleShot(0, this, [this]() {
    // QMainWindow repositions the menu widget during startup, so rebind the
    // traffic-light anchor once after show to pick up the final rect.
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
  connect(m_menu_bar, &MenuBar::resetDockLayout, this, &MainWindow::resetDockLayout);

  connect(m_playback_controls, &PlaybackControls::playToggle, m_coll_listview,
          &VGMCollListView::handlePlaybackRequest);
  connect(m_coll_listview, &VGMCollListView::nothingToPlay, m_playback_controls,
          &PlaybackControls::showPlayInfo);
  connect(m_playback_controls, &PlaybackControls::stopPressed, m_coll_listview,
          &VGMCollListView::handleStopRequest);
  connect(m_playback_controls, &PlaybackControls::seekingTo, &SequencePlayer::the(), &SequencePlayer::seek);
  connect(&qtVGMRoot, &QtVGMRoot::UI_toastRequested, this, &MainWindow::showToast);

  auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, m_coll_listview, &VGMCollListView::handlePlaybackRequest);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *widget = qobject_cast<QWidget *>(obj);
    if (mouseEvent->button() == Qt::LeftButton && widget && (widget == this || isAncestorOf(widget)) &&
        isDockSeparatorCursor(cursor().shape())) {
      m_dockSeparatorDragActive = true;
    }
  } else if (event->type() == QEvent::MouseMove) {
    if (m_dockSeparatorDragActive) {
      QTimer::singleShot(0, this, [this]() { updateCollectionContentsWidthLock(); });
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton && m_dockSeparatorDragActive) {
      m_dockSeparatorDragActive = false;
      scheduleDockStateUpdate();
    }
  }

  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!keyEvent->isAutoRepeat() && keyEvent->key() == HexViewInput::kModifierKey) {
      const bool active = event->type() == QEvent::KeyPress;
      NotificationCenter::the()->setSeekModifierActive(active);
    }
  } else if (event->type() == QEvent::ApplicationDeactivate) {
    m_dockSeparatorDragActive = false;
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

void MainWindow::closeEvent(QCloseEvent *event) {
  m_closingDown = true;
  m_dockSeparatorDragActive = false;
  m_savedDockState = saveState(kDockLayoutStateVersion);
  saveLayoutSettings();
  QMainWindow::closeEvent(event);
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
  const bool widthChanged = event->oldSize().width() >= 0 && event->size().width() != event->oldSize().width();
  const bool heightChanged = event->oldSize().height() >= 0 && event->size().height() != event->oldSize().height();
  const bool widthExpanded = event->oldSize().width() >= 0 && event->size().width() > event->oldSize().width();
  const bool heightExpanded = event->oldSize().height() >= 0 && event->size().height() > event->oldSize().height();

  QMainWindow::resizeEvent(event);
  updateDragOverlayGeometry();

  if (!widthChanged && !heightChanged) {
    return;
  }

  applyDockAreaTargets(widthExpanded, heightExpanded);
  updateCollectionContentsWidthLock();
  activateMainLayout();
}

void MainWindow::updateDragOverlayAppearance() {
  m_dragOverlay->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 102);"));
}

void MainWindow::updateDragOverlayGeometry() {
  m_dragOverlay->setGeometry(rect());
  m_dragOverlay->raise();
}
