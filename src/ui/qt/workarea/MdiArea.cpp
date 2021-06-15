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

  auto *tab_bar = findChild<QTabBar *>();
  if (tab_bar) {
    tab_bar->setExpanding(false);
    tab_bar->setUsesScrollButtons(true);
  }
}

void MdiArea::NewView(VGMFile *file) {
  auto it = m_registered_views.find(file);
  // Check if a fileview for this vgmfile already exists
  if (it != m_registered_views.end()) {
    // If it does, let's focus it
    auto *vgmfile_view = it->second;
    vgmfile_view->setFocus();
  } else {
    // No VGMFileView could be found, we have to make one
    auto *vgmfile_view = new VGMFileView(file);
    auto tab = addSubWindow(vgmfile_view, Qt::SubWindow);
    tab->show();

    m_registered_views.insert(std::make_pair(file, tab));
  }
}

void MdiArea::RemoveView(VGMFile *file) {
  // Let's check if we have a VGMFileView to remove
  auto it = m_registered_views.find(file);
  if (it != m_registered_views.end()) {
    // Sanity check
    if (it->second) {
      // Close the tab (automatically deletes it)
      // Workaround for QTBUG-5446 (removeMdiSubWindow would be a better option)
      it->second->close();
    }
    // Get rid of the saved pointers
    m_registered_views.erase(file);
  }
}
