/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QToolBar>
#include <QIcon>

class QSlider;
class QTimer;
class QLabel;

class ToolBar : public QToolBar {
    Q_OBJECT

   public:
    explicit ToolBar(QWidget *parent = nullptr);

   signals:
    void OpenPressed();
    void PlayToggle();
    void StopPressed();
    void SeekingTo(int);

   public slots:
    void OnPlayerStatusChange(bool playing);
    void RangeUpdate(int cur, int max);

   private:
    void SetupControls();

    QAction *m_open{};
    QAction *m_prev{};
    QAction *m_play{};
    QAction *m_next{};
    QAction *m_stop{};
    QSlider *m_slider{};
    QLabel *m_title;
    inline static QIcon s_playicon;
    inline static QIcon s_pauseicon;
};
