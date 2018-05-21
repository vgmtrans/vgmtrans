/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#ifndef ICONBAR_H
#define ICONBAR_H

#include <QAction>
#include <QToolBar>
#include <QIcon>

class IconBar : public QToolBar {
  Q_OBJECT

public:
  explicit IconBar(QWidget *parent = nullptr);

signals:
  void OpenPressed();
  void PlayToggle();
  void PausePressed();
  void StopPressed();

private:
  void SetupActions();
  void SetupIcons();

  void OnPlayerStatusChange(const bool playing);

  QAction *iconbar_open;
  QAction *iconbar_play;
  QAction *iconbar_stop;
};

#endif