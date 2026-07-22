/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "main/base/ToastType.h"

#include <QList>
#include <QMainWindow>
#include <QUrl>

#include <filesystem>
#include <span>

class QWidget;
class QDockWidget;
class MenuBar;
class MainWindowDockLayout;
class PlaybackControls;
class SequencePlayer;
class Logger;
class QListView;
class QSortFilterProxyModel;
class StatusBarContent;
class TableView;
class CollectionListView;
class ToastHost;
class WindowBar;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QCloseEvent;
class QResizeEvent;
class QToolButton;
namespace QWK {
class WidgetWindowAgent;
}

namespace vgmtrans::ui {
class CollectionContentsModel;
class WorkspaceController;
}

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(vgmtrans::ui::WorkspaceController& workspace);
  void openPaths(std::span<const std::filesystem::path> paths);
  void showEvent(QShowEvent* event) override;
  void showDragOverlay();
  void hideDragOverlay();
  void handleDroppedUrls(const QList<QUrl>& urls);
  void setCollectionStitchAvailable(bool available);
  void setCollectionStitchOpen(bool open);

public slots:
  void showToast(const QString& message, ToastType type, int durationMs = 3000);

signals:
  void manualCollectionRequested();
  void collectionStitchRequested();
  void seekModifierActiveChanged(bool active);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  enum class SelectionStatusKind {
    Source,
    Asset,
    Collection,
    CollectionContents,
  };

  void createElements();
  void configureWindowAgent();
  void createStatusBar();
  void routeSignals();
  void updateDragOverlayAppearance();
  void updateDragOverlayGeometry();

  void openFile();
  void openFileInternal(const QString& filename);
  void removeSelectedSources();
  void removeSelectedAssets();
  void exportSequenceMidi(const QModelIndex& index);
  void togglePlayback();
  void updateSelectionStatus(const QModelIndex& index,
                             SelectionStatusKind kind);

  vgmtrans::ui::WorkspaceController& m_workspace;
  TableView* m_rawfile_listview{};
  TableView* m_vgmfile_listview{};
  CollectionListView* m_coll_listview{};
  QListView* m_coll_view{};
  QSortFilterProxyModel* m_collection_filter{};
  vgmtrans::ui::CollectionContentsModel* m_collection_contents_model{};

  QDockWidget *m_rawfile_dock{};
  QDockWidget *m_vgmfile_dock{};
  QDockWidget *m_coll_dock{};
  QDockWidget *m_coll_view_dock{};
  MenuBar *m_menu_bar{};
  PlaybackControls *m_playback_controls{};
  SequencePlayer *m_sequence_player{};
  StatusBarContent *statusBarContent{};
  Logger *m_logger{};
  QToolButton *m_stitchButton{};
  ToastHost *m_toastHost{};
  WindowBar *m_windowBar{};
  QWidget *m_dragOverlay{};
  QWK::WidgetWindowAgent *m_windowAgent{};
  MainWindowDockLayout *m_dockLayout{};
};
