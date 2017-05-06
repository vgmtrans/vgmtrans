#include "ToolBar.h"

#include <QIcon>

#include "ToolBar.h"

static QSize ICON_SIZE(32, 32);

ToolBar::ToolBar(QWidget* parent) : QToolBar(parent)
{
  setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  setMovable(false);
  setFloatable(false);
  setIconSize(ICON_SIZE);

  MakeActions();
  UpdateIcons();

//  Unselected();
}

//void ToolBar::VGMFileSelected()
//{
//  playAction->setEnabled(false);
//  playAction->setVisible(false);
//  pauseAction->setEnabled(true);
//  pauseAction->setVisible(true);
//  stopAction->setEnabled(true);
//  stopAction->setVisible(true);
//}

void ToolBar::MakeActions()
{
  constexpr int button_width = 65;
  openAction = addAction(tr("Open"), this, SIGNAL(OpenPressed()));
  widgetForAction(openAction)->setMinimumWidth(button_width);

  addSeparator();

  playAction = addAction(tr("Play"), this, SIGNAL(PlayPressed()));
  widgetForAction(playAction)->setMinimumWidth(button_width);

  pauseAction = addAction(tr("Pause"), this, SIGNAL(PausePressed()));
  widgetForAction(pauseAction)->setMinimumWidth(button_width);

  stopAction = addAction(tr("Stop"), this, SIGNAL(StopPressed()));
  widgetForAction(stopAction)->setMinimumWidth(button_width);
}

void ToolBar::UpdateIcons()
{
//  QString dir = Settings().GetThemeDir();
//
//  openAction->setIcon(QIcon(QStringLiteral("open.png").prepend(dir)));
//  playAction->setIcon(QIcon(QStringLiteral("play.png").prepend(dir)));
//  pauseAction->setIcon(QIcon(QStringLiteral("pause.png").prepend(dir)));
//  stopAction->setIcon(QIcon(QStringLiteral("stop.png").prepend(dir)));
}