/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include <QTabBar>
#include <VGMFile.h>
#include "VGMFileView.h"
#include "Helpers.h"

MdiArea::MdiArea(QWidget *parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);

  connect(this, &QMdiArea::subWindowActivated, this, &MdiArea::onSubWindowActivated);

  auto *tab_bar = findChild<QTabBar *>();
  if (tab_bar) {
    #ifdef __APPLE__
    tab_bar->setExpanding(true);
    #else
    tab_bar->setExpanding(false);
    #endif
    tab_bar->setUsesScrollButtons(true);
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
  }
}

void MdiArea::removeView(VGMFile *file) {
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

void MdiArea::focusView(VGMFile *file, QWidget *caller) {
  auto it = fileToWindowMap.find(file);
  if (it != fileToWindowMap.end()) {
    QMdiSubWindow *window = it->second;
    setActiveSubWindow(window);

    // Reassert the focus back to the caller
    if (caller) {
      caller->setFocus();
    }
  }
}

void MdiArea::onSubWindowActivated(QMdiSubWindow *window) {
  // For some reason, if multiple documents are open, closing one document causes the others
  // to become windowed instead of maximized. This fixes the problem.
  ensureMaximizedSubWindow(window);

  if (window) {
    auto it = windowToFileMap.find(window);
    if (it != windowToFileMap.end()) {
      VGMFile *file = it->second;
      emit vgmFileSelected(file);
    }
  }
}

void MdiArea::ensureMaximizedSubWindow(QMdiSubWindow *window) {
  if (window && !window->isMaximized()) {
    window->showMaximized();
  }
}