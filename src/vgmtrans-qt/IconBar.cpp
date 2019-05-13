/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QToolBar>

#include "util/Helpers.h"
#include "IconBar.h"

auto constexpr icons_size = QSize(16, 16);

IconBar::IconBar(QWidget *parent) : QToolBar(parent) {
  setMovable(false);
  setFloatable(false);
  setIconSize(icons_size);

  SetupActions();
  SetupIcons();
}

void IconBar::SetupActions() {
  iconbar_open = addAction("Open");
  connect(iconbar_open, &QAction::triggered, this, &IconBar::OpenPressed);

  addSeparator();

  iconbar_play = addAction("Play");
  connect(iconbar_play, &QAction::triggered, this, &IconBar::PlayToggle);
  iconbar_stop = addAction("Stop");
  connect(iconbar_stop, &QAction::triggered, this, &IconBar::StopPressed);
}

void IconBar::SetupIcons() {
  iconopen = MakeIconFromPath(":/images/open-32.png");
  iconbar_open->setIcon(iconopen);

  iconplay = MakeIconFromPath(":/images/play-32.png");
  iconpause = MakeIconFromPath(":/images/pause-32.png");
  iconbar_play->setIcon(iconplay);

  iconstop = MakeIconFromPath(":/images/stop-32.png");
  iconbar_stop->setIcon(iconstop);
  iconbar_stop->setDisabled(true);
}

void IconBar::OnPlayerStatusChange(bool playing) {
  if (playing) {
    iconbar_play->setIcon(iconpause);
    iconbar_stop->setEnabled(true);
  } else {
    iconbar_play->setIcon(iconplay);
    iconbar_stop->setDisabled(true);
  }
}
