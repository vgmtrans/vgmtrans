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
#include <QLayout>
#include <QPushButton>

#include "util/Helpers.h"
#include "MusicPlayer.h"

IconBar::IconBar(QWidget *parent) : QWidget(parent) {
  setLayout(new QHBoxLayout());
  layout()->setContentsMargins(0, 0, 0, 0);
  setupControls();
}

void IconBar::setupControls() {
  s_playicon = QIcon(":/images/player_play.svg");
  s_pauseicon = QIcon(":/images/player_pause.svg");

  m_play = new QPushButton();
  m_play->setIcon(s_playicon);
  m_play->setToolTip("Play selected collection (Space)");
  connect(m_play, &QPushButton::pressed, this, &IconBar::playToggle);
  layout()->addWidget(m_play);

  m_stop = new QPushButton();
  m_stop->setIcon(QIcon(":/images/player_stop.svg"));
  m_stop->setDisabled(true);
  m_stop->setToolTip("Stop playback (Esc)");
  connect(m_stop, &QPushButton::pressed, this, &IconBar::stopPressed);
  layout()->addWidget(m_stop);

  m_slider = new QSlider(Qt::Horizontal);
  m_slider->setRange(0, 0);
  m_slider->setEnabled(false);
  m_slider->setToolTip("Seek");
  m_slider->setFixedHeight(30);
  connect(m_slider, &QSlider::sliderMoved, this, &IconBar::seekingTo);
  layout()->addWidget(m_slider);

  m_title = new QLabel("Player stopped");
  layout()->addWidget(m_title);

  connect(&MusicPlayer::the(), &MusicPlayer::playbackPositionChanged, this,
          &IconBar::playbackRangeUpdate);
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
    m_title->setText("Player stopped");
  }
}