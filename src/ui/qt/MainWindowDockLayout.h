#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSize>

class MainWindow;
class QDockWidget;
class QEvent;
class QTimer;
class VGMCollListView;

// Encapsulates MainWindow dock policy, persistence, and layout reconciliation.
class MainWindowDockLayout final : public QObject {
public:
  struct Docks {
    QDockWidget* rawFiles{};
    QDockWidget* vgmFiles{};
    QDockWidget* collections{};
    QDockWidget* collectionContents{};
    QDockWidget* logs{};
    VGMCollListView* collectionListView{};
  };

  MainWindowDockLayout(MainWindow* window, Docks docks);

  void restoreWindowGeometry() const;
  void initializeAfterFirstShow();
  void handleResize(const QSize& oldSize, const QSize& newSize);
  void beginSeparatorDrag();
  void handleSeparatorMouseMove();
  void endSeparatorDrag();
  void cancelInteraction();
  void resetToDefault();
  void saveOnClose();

private:
  struct FloatingDockRedockState {
    QDockWidget* dock{};
    QSize windowSize{};
    int leftAreaWidth{};
    int bottomAreaHeight{};

    [[nodiscard]] bool matches(QDockWidget* candidate) const {
      return candidate && candidate == dock;
    }

    void clear() {
      dock = nullptr;
      windowSize = QSize();
      leftAreaWidth = 0;
      bottomAreaHeight = 0;
    }
  };

  bool eventFilter(QObject* watched, QEvent* event) override;

  enum ReconcileFlag : unsigned {
    ReconcileNone = 0,
    ReconcileSettleLayout = 1u << 0,
    ReconcileApplyAreaTargets = 1u << 1,
    ReconcileUpdateWidthLock = 1u << 2,
  };

  void connectSignals();
  void connectDockSignals(QDockWidget* dock);
  void activateMainLayout();
  bool shouldSkipDockLayoutWork() const;
  QDockWidget* dockForTitleBar(QObject* watched) const;
  void captureDockAreaPreferredSize(const QList<QDockWidget*>& docks, Qt::DockWidgetArea area,
                                    Qt::Orientation orientation, int& preferredSize);
  void captureLeftDockAreaWidth();
  void captureBottomDockAreaHeight();
  void snapshotDockAreaSizes(bool persistState);
  void applyDockAreaTargets(bool applyLeftWidth, bool applyBottomHeight);
  void captureCollectionContentsLeftDockHeight();
  void applyPendingCollectionContentsBottomAreaHeight();
  bool moveCollectionContentsToLeftDockIfNeeded();
  bool moveCollectionContentsToBottomDockIfNeeded();
  bool normalizeCollectionContentsDockPlacement();
  void setCollectionContentsWidthLock(int targetWidth);
  void updateCollectionContentsWidthLock();
  void applyDefaultDockLayout();
  void restoreFloatingDocks();
  void saveLayoutSettings() const;
  void noteBottomDockWillBeShown();
  void handleDockVisibilityChanged();
  void handleDockTopLevelChanged(QDockWidget* dock, bool floating);
  void queuePostRedockSettle(QDockWidget* dock);
  void rememberFloatingDockRedockState(QDockWidget* dock);
  void clearPendingReconcile();
  void requestDockLayoutSettle(bool applyAreaTargets);
  void queueReconcile(unsigned flags);
  bool shouldDeferReconcile(unsigned flags);
  void runFullReconcile(bool applyAreaTargets);
  void processPendingReconcile();
  void applyPendingFloatingDockRedockState(QDockWidget* dock, bool clearState);

  // Cached dock and widget pointers used throughout the layout rules.
  MainWindow* m_window{};
  QDockWidget* m_rawfileDock{};
  QDockWidget* m_vgmfileDock{};
  QDockWidget* m_collectionsDock{};
  QDockWidget* m_collectionContentsDock{};
  QDockWidget* m_loggerDock{};
  VGMCollListView* m_collectionListView{};

  // Precomputed dock groups shared by the placement heuristics.
  QList<QDockWidget*> m_allDocks{};
  QList<QDockWidget*> m_leftAreaDocks{};
  QList<QDockWidget*> m_leftAreaPrimaryDocks{};
  QList<QDockWidget*> m_bottomAreaDocks{};
  QList<QDockWidget*> m_bottomCompanionDocks{};

  // Saved layout state and remembered dock sizes, including the Collection
  // Contents height carried between the left stack and bottom row.
  QByteArray m_defaultDockState{};
  QByteArray m_savedDockState{};
  int m_collectionContentsLeftDockHeight{};
  int m_pendingCollectionContentsBottomHeight{};
  int m_leftDockAreaPreferredWidth{};
  int m_bottomDockAreaPreferredHeight{};

  // Queued reconciliation state used to coalesce dock churn before reacting.
  QTimer* m_reconcileTimer{};
  unsigned m_pendingReconcileFlags{};
  bool m_adjustingDockLayout{};
  bool m_restoringDockState{};
  bool m_closingDown{};
  bool m_dockSeparatorDragActive{};
  bool m_dockWidgetDragActive{};
  FloatingDockRedockState m_pendingFloatingDockRedock{};
};
