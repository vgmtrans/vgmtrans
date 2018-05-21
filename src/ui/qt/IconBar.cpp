/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include <QToolBar>
#include "IconBar.h"

IconBar::IconBar(QWidget *parent) : QToolBar(parent) {
  setMovable(false);
  setFloatable(false);
  setIconSize(QSize(16, 16));

  SetupActions();
  SetupIcons();
}

void IconBar::SetupActions() {
  iconbar_open = addAction(tr("Open"));
  connect(iconbar_open, &QAction::triggered, this, &IconBar::OpenPressed);
  addSeparator();
  iconbar_play = addAction(tr("Play"));
  connect(iconbar_open, &QAction::triggered, this, &IconBar::PlayToggle);
  iconbar_stop = addAction(tr("Stop"));
  connect(iconbar_open, &QAction::triggered, this, &IconBar::StopPressed);
}

void IconBar::SetupIcons() {
  iconbar_open->setIcon(QIcon(":/images/open-32.png"));
  iconbar_play->setIcon(QIcon(":/images/play-32.png"));
  iconbar_stop->setIcon(QIcon(":/images/stop-32.png"));
  iconbar_stop->setDisabled(true);
}

void IconBar::OnPlayerStatusChange(const bool playing) {
  if(playing) {
    iconbar_play->setIcon(QIcon(":/images/pause-32.png"));
    iconbar_stop->setEnabled(true);
  }
  else {
    iconbar_open->setIcon(QIcon(":/images/open-32.png"));
    iconbar_stop->setDisabled(true);
  }
}