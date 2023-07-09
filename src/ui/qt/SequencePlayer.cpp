/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "SequencePlayer.h"
#include <algorithm>
#include <cstddef>

#include "VGMColl.h"
#include "SF2File.h"
#include "VGMSeq.h"
#include "LogManager.h"

/* How often (in ms) the current ticks are polled */
static constexpr auto TICK_POLL_INTERVAL_MS = 10;

SequencePlayer::SequencePlayer() {
  player.initialize();

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      playbackPositionChanged(elapsedSamples(), totalSamples());
    }
  });
//  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);
}

SequencePlayer::~SequencePlayer() {
  stop();
  player.shutdown();
}

void SequencePlayer::toggle() {
  if (playing()) {
    player.pause();
    m_seekupdate_timer->stop();
  } else {
    player.play();
    m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);
  }

  bool status = playing();
  statusChange(status);
}

void SequencePlayer::stop() {
  /* Stop polling seekbar, reset it, propagate that we're done */
  playbackPositionChanged(0, 1);
  statusChange(false);

  /* Stop the audio output */
  player.stop();

  m_active_vgmcoll = nullptr;
}

void SequencePlayer::seek(int position) {
  player.seek(position);
  playbackPositionChanged(position, totalSamples());
}

bool SequencePlayer::playing() const {
  return player.isPlaying();
}

int SequencePlayer::elapsedSamples() const {
  return static_cast<int>(player.elapsedSamples());
}

int SequencePlayer::totalSamples() const {
    return static_cast<int>(player.totalSamples());
}

QString SequencePlayer::songTitle() const {
  return m_song_title;
}

bool SequencePlayer::playCollection(const VGMColl *coll) {
  if (coll == m_active_vgmcoll) {
    toggle();
    return false;
  }

  /* Init soundfont */
  if (! player.loadCollection(coll, [this](){
      QMetaObject::invokeMethod(this, "toggle", Qt::QueuedConnection);
      }))
  {
    L_ERROR("Could not load soundfont. Maybe the system is running out of "
                                  "memory or the sountfont was too large?");
  }

  /* Set callback used to signal that the playback is over */
  static auto stop_callback = [this]() {
    stop();
  };
  player.setSongEndCallback(stop_callback);

  /* Stop the current playback in order to start the new song */
  stop();

  /* Reassign tracking info and start playback */
  m_active_vgmcoll = coll;
//  m_active_stream = midi_stream;
//  m_loaded_sf = sf2_handle;
  m_song_title = QString::fromStdString(*m_active_vgmcoll->name());
//  toggle();

  return true;
}
