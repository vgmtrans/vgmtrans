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
#include <QWhatsThis>
#include <QPushButton>

#include "util/Helpers.h"
#include "MusicPlayer.h"
#include "util/MarqueeLabel.h"

IconBar::IconBar(QWidget *parent) : QWidget(parent) {
  setLayout(new QHBoxLayout());
  layout()->setContentsMargins(0, 0, 0, 0);
  setupControls();
}

void IconBar::setupControls() {
  s_playicon = QIcon(":/images/player_play.svg");
  s_pauseicon = QIcon(":/images/player_pause.svg");

  m_create = new QPushButton("Create collection manually");
  m_create->setAutoDefault(false);
  connect(m_create, &QPushButton::pressed, this, &IconBar::createPressed);
  layout()->addWidget(m_create);

  QFrame *line;
  line = new QFrame(this);
  line->setFrameShape(QFrame::VLine);
  line->setFrameShadow(QFrame::Sunken);
  layout()->addWidget(line);

  m_play = new QPushButton();
  m_play->setIcon(s_playicon);
  m_play->setToolTip("Play selected collection (Space)");
  m_play->setWhatsThis("Select a collection in the panel above and click this \u25b6 button or press 'Space' to play it.\n"
                       "Clicking the button again will pause playback or play a different collection "
                       "if you have changed the selection.");
  connect(m_play, &QPushButton::pressed, this, &IconBar::playToggle);
  layout()->addWidget(m_play);

  m_stop = new QPushButton();
  m_stop->setIcon(QIcon(":/images/player_stop.svg"));
  m_stop->setDisabled(true);
  m_stop->setToolTip("Stop playback (Esc)");
  connect(m_stop, &QPushButton::pressed, this, &IconBar::stopPressed);
  layout()->addWidget(m_stop);

  m_slider = new QSlider(Qt::Horizontal);
  /* Needed to make sure the slider is properly rendered */
  m_slider->setRange(0, 1);
  m_slider->setValue(0);
  #ifdef __APPLE__
  /* HACK: workaround the slider being cut off on macOS */
  m_slider->setFixedHeight(25);
  #endif
  
  m_slider->setEnabled(false);
  m_slider->setToolTip("Seek");
  connect(m_slider, &QSlider::sliderReleased, [this]() { seekingTo(m_slider->value()); });
  layout()->addWidget(m_slider);

  m_title = new MarqueeLabel();
  m_title->setText("Playback interrupted");
  layout()->addWidget(m_title);

  connect(&MusicPlayer::the(), &MusicPlayer::playbackPositionChanged, this,
          &IconBar::playbackRangeUpdate);
  connect(&MusicPlayer::the(), &MusicPlayer::statusChange, this, &IconBar::playerStatusChanged);
}

void IconBar::showPlayInfo() {
  QWhatsThis::showText(m_play->mapToGlobal(m_play->pos()), m_play->whatsThis(), this);
  m_play->clearFocus();
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
    m_title->setText("Playing: " + MusicPlayer::the().songTitle());
  } else {
    m_play->setIcon(s_playicon);
    m_stop->setDisabled(true);
    m_title->setText("Playback interrupted");
  }
}