/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

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
    void StopPressed();

   public slots:
    void OnPlayerStatusChange(bool playing);

   private:
    void SetupActions();
    void SetupIcons();

    QAction *iconbar_open;
    QAction *iconbar_play;
    QAction *iconbar_stop;

    QIcon iconopen;
    QIcon iconplay;
    QIcon iconpause;
    QIcon iconstop;
};
