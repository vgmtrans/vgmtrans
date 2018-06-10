/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"
#include <QTabBar>
#include <QAbstractButton>
#include <QApplication>

MdiArea::MdiArea(QWidget *parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setTabShape(QTabWidget::Triangular);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);

  QTabBar *tab_bar = findChild<QTabBar *>();
  if (tab_bar) {
    tab_bar->setExpanding(false);
    tab_bar->setUsesScrollButtons(true);
  }
}

void MdiArea::RemoveTab(VGMFileView *file_view) {
  auto *file_view_tab = file_view->parentWidget();
  file_view_tab->close();
  removeSubWindow(file_view_tab);
}
