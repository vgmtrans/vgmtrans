/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "IconBar.h"

#include <QToolBar>
#include <QIcon>
#include <QSlider>
#include <QLabel>

#include "util/Helpers.h"
#include "MusicPlayer.h"

IconBar::IconBar(QWidget *parent) : QToolBar(parent) {
    setMovable(false);
    setFloatable(false);

    setupControls();
}

void IconBar::setupControls() {
    m_open = addAction("Open");
    m_open->setIcon(QIcon(":/images/open_file.svg"));
    connect(m_open, &QAction::triggered, this, &IconBar::openPressed);

    addSeparator();

    m_stop = addAction("Stop");
    m_stop->setIcon(QIcon(":/images/player_stop.svg"));
    m_stop->setDisabled(true);
    connect(m_stop, &QAction::triggered, this, &IconBar::stopPressed);

    m_play = addAction("Play");
    s_playicon = QIcon(":/images/player_play.svg");
    s_pauseicon = QIcon(":/images/player_pause.svg");
    m_play->setIcon(s_playicon);
    connect(m_play, &QAction::triggered, this, &IconBar::playToggle);

    /* A bit of a hack to get some spacing */
    auto spacing_label = new QLabel();
    spacing_label->setFixedWidth(4);
    addWidget(spacing_label);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 0);
    m_slider->setEnabled(false);
    m_slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_slider->setMinimumWidth(300);
    m_slider->setToolTip("Seek");
    connect(m_slider, &QSlider::sliderMoved, this, &IconBar::seekingTo);
    addWidget(m_slider);

    auto spacing_label2 = new QLabel();
    spacing_label2->setFixedWidth(6);
    addWidget(spacing_label2);

    m_title = new QLabel();
    addWidget(m_title);

    connect(&MusicPlayer::the(), &MusicPlayer::playbackPositionChanged, this, &IconBar::playbackRangeUpdate);
    connect(&MusicPlayer::the(), &MusicPlayer::statusChange, this, &IconBar::playerStatusChanged);
}

void IconBar::playbackRangeUpdate(int cur, int max) {
    if (max != m_slider->maximum()) {
        m_slider->setRange(0, max);
    }

    if (!m_slider->isSliderDown()) {
        m_slider->setValue(cur);
    }
}

void IconBar::playerStatusChanged(bool playing) {
    if (playing) {
        m_slider->setEnabled(true);
        m_play->setIcon(s_pauseicon);
        m_stop->setEnabled(true);
        m_title->setText("Now playing: " + MusicPlayer::the().songTitle());
    } else {
        m_play->setIcon(s_playicon);
        m_stop->setDisabled(true);
        m_slider->setDisabled(true);
        m_title->setText("");
    }
}