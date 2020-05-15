/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QToolBar>
#include <QIcon>
#include <QSlider>
#include <QLabel>

#include "LogManager.h"
#include "util/Helpers.h"
#include "ToolBar.h"
#include "MusicPlayer.h"

constexpr QSize icons_size(20, 20);

ToolBar::ToolBar(QWidget *parent) : QToolBar(parent) {
    setMovable(false);
    setFloatable(false);
    setIconSize(icons_size);

    SetupControls();
}

void ToolBar::SetupControls() {
    m_open = addAction("Open");
    m_open->setIcon(QIcon(":/images/open_file.svg"));
    connect(m_open, &QAction::triggered, this, &ToolBar::OpenPressed);

    addSeparator();

    m_stop = addAction("Stop");
    m_stop->setIcon(QIcon(":/images/player_stop.svg"));
    m_stop->setDisabled(true);
    connect(m_stop, &QAction::triggered, this, &ToolBar::StopPressed);

    m_prev = addAction("Previous collection");
    m_prev->setIcon(QIcon(":/images/player_prev.svg"));
    connect(m_prev, &QAction::triggered, &MusicPlayer::Instance(), &MusicPlayer::Prev);

    m_play = addAction("Play");
    s_playicon = QIcon(":/images/player_play.svg");
    s_pauseicon = QIcon(":/images/player_pause.svg");
    m_play->setIcon(s_playicon);
    connect(m_play, &QAction::triggered, this, &ToolBar::PlayToggle);

    m_next = addAction("Next collection");
    m_next->setIcon(QIcon(":/images/player_next.svg"));
    connect(m_next, &QAction::triggered, &MusicPlayer::Instance(), &MusicPlayer::Next);

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
    connect(m_slider, &QSlider::sliderMoved, this, &ToolBar::SeekingTo);
    addWidget(m_slider);

    auto spacing_label2 = new QLabel();
    spacing_label2->setFixedWidth(6);
    addWidget(spacing_label2);

    m_title = new QLabel();
    addWidget(m_title);

    connect(&MusicPlayer::Instance(), &MusicPlayer::PositionChanged, this, &ToolBar::RangeUpdate);
}

void ToolBar::RangeUpdate(int cur, int max) {
    if (max != m_slider->maximum()) {
        m_slider->setRange(0, max);
    }

    if (!m_slider->isSliderDown()) {
        m_slider->setValue(cur);
    }
}

void ToolBar::OnPlayerStatusChange(bool playing) {
    if (playing) {
        m_slider->setEnabled(true);
        m_play->setIcon(s_pauseicon);
        m_stop->setEnabled(true);
        m_title->setText("Now playing: " + QString::fromStdString(MusicPlayer::Instance().Title()));
    } else {
        m_play->setIcon(s_playicon);
        m_stop->setDisabled(true);
        m_slider->setDisabled(true);
        m_title->setText("");
    }
}
