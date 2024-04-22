/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include <QTabBar>
#include <QApplication>
#include <QKeyEvent>
#include <VGMFile.h>
#include "VGMFileView.h"
#include "Metrics.h"
#include "services/NotificationCenter.h"

MdiArea::MdiArea(QWidget *parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);

  connect(this, &QMdiArea::subWindowActivated, this, &MdiArea::onSubWindowActivated);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &MdiArea::onVGMFileSelected);

  if (auto *tab_bar = findChild<QTabBar *>()) {
    tab_bar->setStyleSheet(QString{"QTabBar::tab { height: %1; }"}.arg(Size::VTab));
    tab_bar->setExpanding(false);
    tab_bar->setUsesScrollButtons(true);
#ifdef Q_OS_MAC
    tab_bar->setElideMode(Qt::ElideNone);
#endif
  }
}

void MdiArea::newView(VGMFile *file) {
  auto it = fileToWindowMap.find(file);
  // Check if a fileview for this vgmfile already exists
  if (it != fileToWindowMap.end()) {
    // If it does, let's focus it
    auto *vgmfile_view = it->second;
    vgmfile_view->setFocus();
  } else {
    // No VGMFileView could be found, we have to make one
    auto *vgmfile_view = new VGMFileView(file);
    auto tab = addSubWindow(vgmfile_view, Qt::SubWindow);
    fileToWindowMap.insert(std::make_pair(file, tab));
    windowToFileMap.insert(std::make_pair(tab, file));
    tab->showMaximized();
    tab->setFocus();

#ifdef Q_OS_MAC
    auto newTitle = " " + vgmfile_view->windowTitle() + " ";
    vgmfile_view->setWindowTitle(newTitle);
#endif
  }
}

void MdiArea::removeView(const VGMFile *file) {
  // Let's check if we have a VGMFileView to remove
  auto it = fileToWindowMap.find(file);
  if (it != fileToWindowMap.end()) {
    // Sanity check
    if (it->second) {
      // Close the tab (automatically deletes it)
      // Workaround for QTBUG-5446 (removeMdiSubWindow would be a better option)
      it->second->close();
    }
    // Get rid of the saved pointers
    windowToFileMap.erase(it->second);
    fileToWindowMap.erase(file);
  }
}

void MdiArea::onSubWindowActivated(QMdiSubWindow *window) {
  if (!window)
    return;

  // For some reason, if multiple documents are open, closing one document causes the others
  // to become windowed instead of maximized. This fixes the problem.
  ensureMaximizedSubWindow(window);

  // Another quirk: paintEvents for all subWindows, not just the active one, are fired
  // unless we manually hide them.
  for (auto subWindow : subWindowList()) {
    subWindow->widget()->setHidden(subWindow != window);
  }

  if (window) {
    auto it = windowToFileMap.find(window);
    if (it != windowToFileMap.end()) {
      VGMFile *file = it->second;
      NotificationCenter::the()->selectVGMFile(file, this);
    }
  }
}

void MdiArea::onVGMFileSelected(const VGMFile *file, QWidget *caller) {
  if (caller == this || file == nullptr)
    return;

  auto it = fileToWindowMap.find(file);
  if (it != fileToWindowMap.end()) {

    QWidget* focusedWidget = QApplication::focusWidget();
    bool callerHadFocus = focusedWidget && caller && caller->isAncestorOf(focusedWidget);
    QMdiSubWindow *window = it->second;
    setActiveSubWindow(window);

    // Reassert the focus back to the caller
    if (caller && callerHadFocus) {
      caller->setFocus();
    }
  }
}

void MdiArea::keyPressEvent(QKeyEvent* event) {
  QMdiArea::keyPressEvent(event);

  // Handle MacOS shortcut for switching tabs
  if ((event->modifiers() & Qt::ShiftModifier) && (event->modifiers() & Qt::ControlModifier)) {
    switch (event->key()) {
      case Qt::Key_BracketLeft:
        this->activatePreviousSubWindow();
        break;
      case Qt::Key_BracketRight:
        this->activateNextSubWindow();
        break;
    }
  }
}

void MdiArea::ensureMaximizedSubWindow(QMdiSubWindow *window) {
  if (window && !window->isMaximized()) {
    window->showMaximized();
  }
}