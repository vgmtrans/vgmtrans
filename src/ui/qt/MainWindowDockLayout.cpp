#include "MainWindowDockLayout.h"

#include <QAction>
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QSize>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <utility>
#include "MainWindow.h"
#include "services/Settings.h"
#include "util/Metrics.h"
#include "util/UIHelpers.h"
#include "widgets/ItemViewDensity.h"
#include "workarea/VGMCollListView.h"

namespace {
constexpr int kDockLayoutStateVersion = 2;

bool isVisibleDockInArea(const QMainWindow *window, QDockWidget *dock, Qt::DockWidgetArea area) {
  return dock && dock->isVisible() && !dock->isFloating() && window->dockWidgetArea(dock) == area;
}

bool hasVisibleDockInArea(const QMainWindow *window, const QList<QDockWidget *>& docks, Qt::DockWidgetArea area) {
  for (QDockWidget *dock : docks) {
    if (isVisibleDockInArea(window, dock, area)) {
      return true;
    }
  }
  return false;
}

QDockWidget *firstVisibleDockInArea(const QMainWindow *window, const QList<QDockWidget *>& docks,
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

int firstVisibleDockSizeInArea(const QMainWindow *window, const QList<QDockWidget *>& docks,
                               Qt::DockWidgetArea area, Qt::Orientation orientation) {
  if (QDockWidget *dock = firstVisibleDockInArea(window, docks, area)) {
    return dockSizeForOrientation(dock, orientation);
  }
  return 0;
}

bool isLeftMostDockInArea(const QMainWindow *window, QDockWidget *dock, const QList<QDockWidget *>& docks,
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

QDockWidget *leftMostDockInArea(const QMainWindow *window, const QList<QDockWidget *>& docks,
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

QDockWidget *bottomMostDockInArea(const QMainWindow *window, const QList<QDockWidget *>& docks,
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

QList<QDockWidget *> visibleDocksInAreaSorted(const QMainWindow *window, const QList<QDockWidget *>& docks,
                                              Qt::DockWidgetArea area, Qt::Orientation orientation) {
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
}  // namespace

MainWindowDockLayout::MainWindowDockLayout(MainWindow *window, Docks docks)
    : QObject(window),
      m_window(window),
      m_rawfileDock(docks.rawFiles),
      m_vgmfileDock(docks.vgmFiles),
      m_collectionsDock(docks.collections),
      m_collectionContentsDock(docks.collectionContents),
      m_loggerDock(docks.logs),
      m_collectionListView(docks.collectionListView),
      m_allDocks{m_rawfileDock, m_vgmfileDock, m_collectionsDock, m_collectionContentsDock, m_loggerDock},
      m_leftAreaDocks{m_rawfileDock, m_vgmfileDock, m_collectionsDock, m_collectionContentsDock},
      m_leftAreaPrimaryDocks{m_rawfileDock, m_vgmfileDock, m_collectionsDock},
      m_bottomAreaDocks{m_collectionContentsDock, m_collectionsDock, m_loggerDock},
      m_bottomCompanionDocks{m_collectionsDock, m_loggerDock},
      m_savedDockState(Settings::the()->mainWindow.dockState()),
      m_reconcileTimer(new QTimer(this)) {
  m_reconcileTimer->setSingleShot(true);
  connect(m_reconcileTimer, &QTimer::timeout, this, &MainWindowDockLayout::processPendingReconcile);
  connectSignals();
}

void MainWindowDockLayout::restoreWindowGeometry() const {
  if (const QByteArray geometry = Settings::the()->mainWindow.windowGeometry(); !geometry.isEmpty()) {
    m_window->restoreGeometry(geometry);
  }
}

void MainWindowDockLayout::initializeAfterFirstShow() {
  if (!m_defaultDockState.isEmpty()) {
    return;
  }

  m_restoringDockState = true;
  applyDefaultDockLayout();
  m_defaultDockState = m_window->saveState(kDockLayoutStateVersion);

  if (!m_savedDockState.isEmpty() && !m_window->restoreState(m_savedDockState, kDockLayoutStateVersion)) {
    m_savedDockState.clear();
  }
  restoreFloatingDocks();

  if (m_savedDockState.isEmpty()) {
    m_savedDockState = m_defaultDockState;
  }

  m_restoringDockState = false;
  snapshotDockAreaSizes(false);
  updateCollectionContentsWidthLock();
}

void MainWindowDockLayout::handleResize(const QSize& oldSize, const QSize& newSize) {
  const bool widthChanged = oldSize.width() >= 0 && newSize.width() != oldSize.width();
  const bool heightChanged = oldSize.height() >= 0 && newSize.height() != oldSize.height();
  const bool widthExpanded = oldSize.width() >= 0 && newSize.width() > oldSize.width();
  const bool heightExpanded = oldSize.height() >= 0 && newSize.height() > oldSize.height();

  if (!widthChanged && !heightChanged) {
    return;
  }

  applyDockAreaTargets(widthExpanded, heightExpanded);
  updateCollectionContentsWidthLock();
  activateMainLayout();
}

void MainWindowDockLayout::beginSeparatorDrag() {
  m_dockSeparatorDragActive = true;
}

void MainWindowDockLayout::handleSeparatorMouseMove() {
  if (!m_dockSeparatorDragActive) {
    return;
  }

  queueReconcile(ReconcileUpdateWidthLock);
}

void MainWindowDockLayout::endSeparatorDrag() {
  if (!m_dockSeparatorDragActive) {
    return;
  }

  m_dockSeparatorDragActive = false;
  requestDockLayoutSettle(false);
}

void MainWindowDockLayout::cancelInteraction() {
  m_dockSeparatorDragActive = false;
}

void MainWindowDockLayout::resetToDefault() {
  if (m_defaultDockState.isEmpty()) {
    return;
  }

  cancelInteraction();
  m_pendingReconcileFlags = ReconcileNone;
  m_reconcileTimer->stop();

  m_restoringDockState = true;
  const bool restored = m_window->restoreState(m_defaultDockState, kDockLayoutStateVersion);
  if (restored) {
    applyDefaultDockLayout();
  }
  m_restoringDockState = false;

  if (!restored) {
    return;
  }

  snapshotDockAreaSizes(true);
  updateCollectionContentsWidthLock();
  saveLayoutSettings();
}

void MainWindowDockLayout::saveOnClose() {
  m_closingDown = true;
  cancelInteraction();
  m_pendingReconcileFlags = ReconcileNone;
  m_reconcileTimer->stop();
  m_savedDockState = m_window->saveState(kDockLayoutStateVersion);
  saveLayoutSettings();
}

void MainWindowDockLayout::connectSignals() {
  for (QDockWidget *dock : m_allDocks) {
    if (!dock) {
      continue;
    }

    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) { requestDockLayoutSettle(false); });
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { requestDockLayoutSettle(false); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
      requestDockLayoutSettle(!floating);
    });
  }

  connect(m_collectionsDock->toggleViewAction(), &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      noteBottomDockWillBeShown();
    }
  });
  connect(m_loggerDock->toggleViewAction(), &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      noteBottomDockWillBeShown();
    }
  });
}

void MainWindowDockLayout::activateMainLayout() {
  if (QLayout *mainLayout = m_window->layout()) {
    mainLayout->activate();
  }
}

void MainWindowDockLayout::captureLeftDockAreaWidth() {
  if (const int dockSize =
          firstVisibleDockSizeInArea(m_window, m_leftAreaDocks, Qt::LeftDockWidgetArea, Qt::Horizontal);
      dockSize > 0) {
    m_leftDockAreaPreferredWidth = dockSize;
  }
}

void MainWindowDockLayout::captureBottomDockAreaHeight() {
  if (const int dockSize =
          firstVisibleDockSizeInArea(m_window, m_bottomAreaDocks, Qt::BottomDockWidgetArea, Qt::Vertical);
      dockSize > 0) {
    m_bottomDockAreaPreferredHeight = dockSize;
  }
}

void MainWindowDockLayout::snapshotDockAreaSizes(bool persistState) {
  activateMainLayout();
  captureLeftDockAreaWidth();
  captureBottomDockAreaHeight();
  if (persistState) {
    m_savedDockState = m_window->saveState(kDockLayoutStateVersion);
  }
}

void MainWindowDockLayout::applyDockAreaTargets(bool applyLeftWidth, bool applyBottomHeight) {
  bool resized = false;
  const auto resizeAreaToPreferredSize =
      [this, &resized](const QList<QDockWidget *>& docks, Qt::DockWidgetArea area,
                       Qt::Orientation orientation, int preferredSize) {
        if (preferredSize <= 0) {
          return;
        }

        if (QDockWidget *dock = firstVisibleDockInArea(m_window, docks, area)) {
          m_window->resizeDocks({dock}, {preferredSize}, orientation);
          resized = true;
        }
      };

  if (applyLeftWidth) {
    resizeAreaToPreferredSize(m_leftAreaDocks, Qt::LeftDockWidgetArea, Qt::Horizontal, m_leftDockAreaPreferredWidth);
  }

  if (applyBottomHeight) {
    resizeAreaToPreferredSize(m_bottomAreaDocks, Qt::BottomDockWidgetArea, Qt::Vertical,
                              m_bottomDockAreaPreferredHeight);
  }

  if (resized) {
    activateMainLayout();
  }
}

void MainWindowDockLayout::captureCollectionContentsLeftDockHeight() {
  if (m_defaultDockState.isEmpty() || m_adjustingDockLayout || m_restoringDockState || m_closingDown) {
    return;
  }

  if (isVisibleDockInArea(m_window, m_collectionContentsDock, Qt::LeftDockWidgetArea) &&
      bottomMostDockInArea(m_window, m_leftAreaDocks, Qt::LeftDockWidgetArea) == m_collectionContentsDock &&
      !hasVisibleDockInArea(m_window, m_bottomCompanionDocks, Qt::BottomDockWidgetArea)) {
    m_collectionContentsLeftDockHeight = m_collectionContentsDock->height();
    m_bottomDockAreaPreferredHeight = m_collectionContentsLeftDockHeight;
    m_pendingCollectionContentsBottomHeight = m_collectionContentsLeftDockHeight;
  }
}

void MainWindowDockLayout::applyPendingCollectionContentsBottomAreaHeight() {
  if (m_restoringDockState || m_closingDown || m_pendingCollectionContentsBottomHeight <= 0 ||
      !isVisibleDockInArea(m_window, m_collectionContentsDock, Qt::BottomDockWidgetArea)) {
    return;
  }

  m_bottomDockAreaPreferredHeight = m_pendingCollectionContentsBottomHeight;
  applyDockAreaTargets(false, true);
  activateMainLayout();
  m_pendingCollectionContentsBottomHeight = 0;
}

bool MainWindowDockLayout::moveCollectionContentsToLeftDockIfNeeded() {
  if (!hasVisibleDockInArea(m_window, m_leftAreaPrimaryDocks, Qt::LeftDockWidgetArea) ||
      !isLeftMostDockInArea(m_window, m_collectionContentsDock, m_bottomAreaDocks, Qt::BottomDockWidgetArea) ||
      hasVisibleDockInArea(m_window, m_bottomCompanionDocks, Qt::BottomDockWidgetArea)) {
    return false;
  }

  QList<QDockWidget *> leftDocks =
      visibleDocksInAreaSorted(m_window, m_leftAreaPrimaryDocks, Qt::LeftDockWidgetArea, Qt::Vertical);
  QDockWidget *anchorDock = leftDocks.isEmpty() ? nullptr : leftDocks.constLast();
  if (!anchorDock) {
    return false;
  }

  const int collectionContentsHeight = m_collectionContentsDock->height();
  QList<int> leftDockHeights;
  leftDockHeights.reserve(leftDocks.size() + 1);
  for (QDockWidget *dock : leftDocks) {
    leftDockHeights.append(dock->height());
  }
  leftDocks.append(m_collectionContentsDock);
  leftDockHeights.append(collectionContentsHeight);

  m_collectionContentsLeftDockHeight = collectionContentsHeight;
  m_pendingCollectionContentsBottomHeight = 0;
  m_adjustingDockLayout = true;
  m_collectionContentsDock->setMinimumWidth(0);
  m_collectionContentsDock->setMaximumWidth(QWIDGETSIZE_MAX);
  m_window->splitDockWidget(anchorDock, m_collectionContentsDock, Qt::Vertical);
  activateMainLayout();
  m_window->resizeDocks(leftDocks, leftDockHeights, Qt::Vertical);
  activateMainLayout();
  m_adjustingDockLayout = false;
  return true;
}

bool MainWindowDockLayout::moveCollectionContentsToBottomDockIfNeeded() {
  if (!isVisibleDockInArea(m_window, m_collectionContentsDock, Qt::LeftDockWidgetArea) ||
      bottomMostDockInArea(m_window, m_leftAreaDocks, Qt::LeftDockWidgetArea) != m_collectionContentsDock ||
      !hasVisibleDockInArea(m_window, m_bottomCompanionDocks, Qt::BottomDockWidgetArea)) {
    return false;
  }

  QDockWidget *anchorDock = leftMostDockInArea(m_window, m_bottomCompanionDocks, Qt::BottomDockWidgetArea);
  if (!anchorDock) {
    return false;
  }

  const int collectionContentsWidth = m_collectionContentsDock->width();
  const int collectionContentsHeight = m_collectionContentsLeftDockHeight > 0 ? m_collectionContentsLeftDockHeight
                                                                              : m_collectionContentsDock->height();

  m_pendingCollectionContentsBottomHeight = collectionContentsHeight;
  m_adjustingDockLayout = true;
  m_collectionContentsDock->setMinimumWidth(0);
  m_collectionContentsDock->setMaximumWidth(QWIDGETSIZE_MAX);
  m_window->addDockWidget(Qt::BottomDockWidgetArea, m_collectionContentsDock);
  m_window->splitDockWidget(m_collectionContentsDock, anchorDock, Qt::Horizontal);
  activateMainLayout();

  m_bottomDockAreaPreferredHeight = collectionContentsHeight;
  QList<QDockWidget *> bottomDocks;
  QList<int> bottomDockHeights;
  for (QDockWidget *dock : m_bottomAreaDocks) {
    if (isVisibleDockInArea(m_window, dock, Qt::BottomDockWidgetArea)) {
      bottomDocks.append(dock);
      bottomDockHeights.append(collectionContentsHeight);
    }
  }
  if (!bottomDocks.isEmpty()) {
    m_window->resizeDocks(bottomDocks, bottomDockHeights, Qt::Vertical);
  }
  m_window->resizeDocks({m_collectionContentsDock}, {collectionContentsWidth}, Qt::Horizontal);
  activateMainLayout();
  m_adjustingDockLayout = false;
  return true;
}

bool MainWindowDockLayout::normalizeCollectionContentsDockPlacement() {
  if (m_adjustingDockLayout || m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return false;
  }

  return moveCollectionContentsToLeftDockIfNeeded() || moveCollectionContentsToBottomDockIfNeeded();
}

void MainWindowDockLayout::updateCollectionContentsWidthLock() {
  constexpr int kUnlockedMinimumWidth = 0;
  constexpr int kUnlockedMaximumWidth = QWIDGETSIZE_MAX;

  const bool shouldLockWidth =
      hasVisibleDockInArea(m_window, m_leftAreaDocks, Qt::LeftDockWidgetArea) &&
      isLeftMostDockInArea(m_window, m_collectionContentsDock, m_bottomAreaDocks, Qt::BottomDockWidgetArea);

  if (!shouldLockWidth) {
    m_collectionContentsDock->setMinimumWidth(kUnlockedMinimumWidth);
    m_collectionContentsDock->setMaximumWidth(kUnlockedMaximumWidth);
    return;
  }

  const int targetWidth =
      firstVisibleDockSizeInArea(m_window, m_leftAreaDocks, Qt::LeftDockWidgetArea, Qt::Horizontal);
  if (targetWidth <= 0) {
    m_collectionContentsDock->setMinimumWidth(kUnlockedMinimumWidth);
    m_collectionContentsDock->setMaximumWidth(kUnlockedMaximumWidth);
    return;
  }

  m_collectionContentsDock->setMinimumWidth(targetWidth);
  m_collectionContentsDock->setMaximumWidth(targetWidth);
}

void MainWindowDockLayout::applyDefaultDockLayout() {
  m_collectionContentsLeftDockHeight = 0;
  m_pendingCollectionContentsBottomHeight = 0;
  m_rawfileDock->show();
  m_vgmfileDock->show();
  m_collectionsDock->show();
  m_collectionContentsDock->show();
  m_loggerDock->show();
  activateMainLayout();

  const int bottomDockAreaHeight = Size::VTab + horizontalScrollBarReservedHeight(m_collectionListView) +
      static_cast<int>(4.5 * ItemViewDensity::listItemStride(m_collectionListView));

  m_window->resizeDocks({m_rawfileDock, m_vgmfileDock}, {26, 74}, Qt::Vertical);
  m_window->resizeDocks({m_collectionContentsDock, m_collectionsDock, m_loggerDock},
                        {bottomDockAreaHeight, bottomDockAreaHeight, bottomDockAreaHeight}, Qt::Vertical);
  m_window->resizeDocks({m_collectionContentsDock, m_collectionsDock, m_loggerDock}, {27, 38, 35},
                        Qt::Horizontal);
  activateMainLayout();
  m_loggerDock->hide();
}

void MainWindowDockLayout::restoreFloatingDocks() {
  QTimer::singleShot(0, this, [this]() {
    for (QDockWidget *dock : m_allDocks) {
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

void MainWindowDockLayout::saveLayoutSettings() const {
  Settings::the()->mainWindow.setWindowGeometry(m_window->saveGeometry());
  for (QDockWidget *dock : m_allDocks) {
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

void MainWindowDockLayout::noteBottomDockWillBeShown() {
  captureCollectionContentsLeftDockHeight();
}

void MainWindowDockLayout::requestDockLayoutSettle(bool applyAreaTargets) {
  if (m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return;
  }

  unsigned flags = ReconcileSettleLayout | ReconcileUpdateWidthLock;
  if (applyAreaTargets) {
    flags |= ReconcileApplyAreaTargets;
  }
  queueReconcile(flags);
}

void MainWindowDockLayout::queueReconcile(unsigned flags) {
  if (flags == ReconcileNone || m_defaultDockState.isEmpty() || m_restoringDockState || m_closingDown) {
    return;
  }

  m_pendingReconcileFlags |= flags;
  if (!m_reconcileTimer->isActive()) {
    m_reconcileTimer->start(0);
  }
}

void MainWindowDockLayout::processPendingReconcile() {
  if (m_pendingReconcileFlags == ReconcileNone || m_defaultDockState.isEmpty() || m_restoringDockState ||
      m_closingDown) {
    return;
  }

  const unsigned flags = std::exchange(m_pendingReconcileFlags, ReconcileNone);
  if (m_adjustingDockLayout) {
    queueReconcile(flags);
    return;
  }

  if ((flags & ReconcileSettleLayout) != 0u) {
    const bool applyAreaTargets = (flags & ReconcileApplyAreaTargets) != 0u;
    runWithUpdatesSuspended(m_window, [this, applyAreaTargets]() {
      activateMainLayout();
      const bool normalizedCollectionContents = normalizeCollectionContentsDockPlacement();
      const bool applyBottomHeight = applyAreaTargets ||
          (normalizedCollectionContents &&
           isVisibleDockInArea(m_window, m_collectionContentsDock, Qt::BottomDockWidgetArea));
      if (applyAreaTargets || applyBottomHeight) {
        applyDockAreaTargets(applyAreaTargets, applyBottomHeight);
      }
      applyPendingCollectionContentsBottomAreaHeight();
      captureCollectionContentsLeftDockHeight();
      updateCollectionContentsWidthLock();
      activateMainLayout();
      snapshotDockAreaSizes(true);
    });
    return;
  }

  if ((flags & ReconcileUpdateWidthLock) != 0u) {
    updateCollectionContentsWidthLock();
  }
}
