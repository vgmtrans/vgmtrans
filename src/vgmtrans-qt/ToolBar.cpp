/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QToolBar>
#include <QTimer>
#include <QIcon>
#include <QSlider>
#include <QTimeLine>

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
    m_open->setIcon(MakeIconFromPath(":/images/open-32.png"));
    connect(m_open, &QAction::triggered, this, &ToolBar::OpenPressed);

    addSeparator();

    m_play = addAction("Play");
    m_play->setIcon(MakeIconFromPath(":/images/play-32.png"));
    connect(m_play, &QAction::triggered, this, &ToolBar::PlayToggle);
    m_stop = addAction("Stop");
    m_stop->setIcon(MakeIconFromPath(":/images/stop-32.png"));
    m_stop->setDisabled(true);
    connect(m_stop, &QAction::triggered, this, &ToolBar::StopPressed);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 0);
    m_slider->setEnabled(false);
    m_slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_slider->setMinimumWidth(300);
    connect(m_slider, &QSlider::sliderMoved, this, &ToolBar::SeekingTo);
    addWidget(m_slider);

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
        m_play->setIcon(MakeIconFromPath(":/images/pause-32.png"));
        m_stop->setEnabled(true);
    } else {
        m_play->setIcon(MakeIconFromPath(":/images/play-32.png"));
        m_stop->setDisabled(true);
        m_slider->setDisabled(true);
    }
}
