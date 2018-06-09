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

  QTabBar *tabBar = getTabBar();
  if (tabBar) {
    tabBar->setExpanding(false);
    tabBar->setUsesScrollButtons(true);
  }
}

QTabBar *MdiArea::getTabBar() {
  QList<QTabBar *> tabBarList = findChildren<QTabBar *>();
  QTabBar *tabBar = tabBarList.at(0);

  return tabBarList.at(0);
}