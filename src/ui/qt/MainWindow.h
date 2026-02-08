/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QList>
#include <QMainWindow>
#include <QUrl>
#include "Root.h"

class QWidget;
class QDockWidget;
class MenuBar;
class IconBar;
class Logger;
class VGMCollListView;
class VGMCollView;
class QPushButton;
class StatusBarContent;
class ToastHost;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QResizeEvent;
#if defined(Q_OS_LINUX)
class QRhiWidget;
#endif

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
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void createElements();
  void createStatusBar();
  void routeSignals();
  void updateDragOverlayAppearance();
  void updateDragOverlayGeometry();

  void openFile();
  void openFileInternal(const QString& filename);
  void showToast(const QString& message, ToastType type, int duration_ms);

  QDockWidget *m_rawfile_dock{};
  QDockWidget *m_vgmfile_dock{};
  QDockWidget *m_coll_dock{};
  MenuBar *m_menu_bar{};
  IconBar *m_icon_bar{};
  StatusBarContent *statusBarContent{};
  Logger *m_logger{};
  VGMCollListView *m_coll_listview{};
  VGMCollView *m_coll_view{};
  QPushButton *m_manual_creation_btn{};
  ToastHost *m_toastHost{};
  QWidget *m_dragOverlay{};
#if defined(Q_OS_LINUX)
  QRhiWidget *m_rhiPrimer{};
#endif
};
