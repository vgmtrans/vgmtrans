/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QUrl>
#include "Root.h"

class QWidget;
class QDockWidget;
class MenuBar;
class PlaybackControls;
class Logger;
class VGMCollListView;
class VGMCollView;
class StatusBarContent;
class ToastHost;
class WindowBar;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QCloseEvent;
class QResizeEvent;
namespace QWK {
class WidgetWindowAgent;
}

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  void showEvent(QShowEvent* event) override;
  void showDragOverlay();
  void hideDragOverlay();
  void handleDroppedUrls(const QList<QUrl>& urls);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void createElements();
  void configureWindowAgent();
  void createStatusBar();
  void routeSignals();
  void activateMainLayout();
  void captureLeftDockAreaWidth();
  void captureBottomDockAreaHeight();
  void applyDockAreaTargets(bool applyLeftWidth, bool applyBottomHeight);
  void captureCollectionContentsLeftDockHeight();
  void applyPendingCollectionContentsBottomAreaHeight();
  bool moveCollectionContentsToLeftDockIfNeeded();
  bool moveCollectionContentsToBottomDockIfNeeded();
  bool normalizeCollectionContentsDockPlacement();
  void syncDockLayoutState(bool persistState);
  void settleDockLayoutChange(bool applyAreaTargets);
  void updateCollectionContentsWidthLock();
  void scheduleDockStateUpdate();
  void applyDefaultDockLayout();
  void showRestoredFloatingDocks();
  void resetDockLayout();
  void saveLayoutSettings() const;
  void updateDragOverlayAppearance();
  void updateDragOverlayGeometry();

  void openFile();
  void openFileInternal(const QString& filename);
  void showToast(const QString& message, ToastType type, int duration_ms);

  QDockWidget *m_rawfile_dock{};
  QDockWidget *m_vgmfile_dock{};
  QDockWidget *m_coll_dock{};
  QDockWidget *m_coll_view_dock{};
  MenuBar *m_menu_bar{};
  PlaybackControls *m_playback_controls{};
  StatusBarContent *statusBarContent{};
  Logger *m_logger{};
  VGMCollListView *m_coll_listview{};
  VGMCollView *m_coll_view{};
  ToastHost *m_toastHost{};
  WindowBar *m_windowBar{};
  QWidget *m_dragOverlay{};
  QWK::WidgetWindowAgent *m_windowAgent{};
  QByteArray m_defaultDockState{};
  QByteArray m_savedDockState{};
  int m_collectionContentsLeftDockHeight{};
  int m_pendingCollectionContentsBottomHeight{};
  int m_leftDockAreaPreferredWidth{};
  int m_bottomDockAreaPreferredHeight{};
  bool m_adjustingDockLayout{};
  bool m_restoringDockState{};
  bool m_closingDown{};
  bool m_dockSeparatorDragActive{};
};
