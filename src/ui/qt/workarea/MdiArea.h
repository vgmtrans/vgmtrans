/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <unordered_map>
#include <QMdiArea>
#include <QMdiSubWindow>

class VGMFile;
class VGMFileView;
class QAction;
class QEvent;
class QObject;
class QPaintEvent;
class QTabBar;
class QToolButton;
class QWidget;

class MdiArea : public QMdiArea {
  Q_OBJECT
public:
  /*
   * This singleton is returned as a pointer instead of a reference
   * so that it can be disposed by Qt's destructor machinery
   */
  static auto the() {
    static MdiArea *area = new MdiArea();
    return area;
  }

  MdiArea(const MdiArea &) = delete;
  MdiArea &operator=(const MdiArea &) = delete;
  MdiArea(MdiArea &&) = delete;
  MdiArea &operator=(MdiArea &&) = delete;

  void newView(VGMFile *file);
  void removeView(const VGMFile *file);

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  struct PaneActions {
    QAction *hex = nullptr;
    QAction *tree = nullptr;
    QAction *activeNotes = nullptr;
    QAction *pianoRoll = nullptr;
  };

  MdiArea(QWidget *parent = nullptr);
  void setupTabBarControls();
  void updateTabBarControls();
  void repositionTabBarControls();
  void applyTabBarStyle();
  void refreshTabControlAppearance();
  void updateBackgroundColor();
  static VGMFileView *asFileView(QMdiSubWindow *window);
  void onSubWindowActivated(QMdiSubWindow *window);
  void onVGMFileSelected(const VGMFile *file, QWidget *caller);
  static void ensureMaximizedSubWindow(QMdiSubWindow *window);

  QTabBar *m_tabBar = nullptr;
  QWidget *m_tabBarHost = nullptr;
  QWidget *m_tabControls = nullptr;
  QToolButton *m_singlePaneButton = nullptr;
  QToolButton *m_leftPaneButton = nullptr;
  QToolButton *m_rightPaneButton = nullptr;
  PaneActions m_leftPaneActions{};
  PaneActions m_rightPaneActions{};
  int m_reservedTabBarRightMargin = 0;
  QPixmap m_cachedStableTabBarColumn;

  std::unordered_map<const VGMFile *, QMdiSubWindow *> fileToWindowMap;
  std::unordered_map<QMdiSubWindow *, VGMFile *> windowToFileMap;
};
