/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "SequencePlayer.h"

#include "LogManager.h"
#include "QtVGMRoot.h"
#include "SF2Conversion.h"
#include "SF2File.h"
#include "VGMColl.h"
#include "VGMSeq.h"

/* How often (in ms) the current ticks are polled */
static constexpr auto TICK_POLL_INTERVAL_MS = 1000/60;

JUCE_IMPLEMENT_SINGLETON (SequencePlayer)

SequencePlayer::SequencePlayer() {
  player.initialize();

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      playbackPositionChanged(elapsedSamples(), totalSamples(), PositionChangeOrigin::Playback);
    }
  });
  //  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);
  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);

  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, [this](VGMColl* coll) {
    if (m_active_vgmcoll == coll) {
      stop();
    }
  });
}

SequencePlayer::~SequencePlayer() {
  clearSingletonInstance();
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
  playbackPositionChanged(0, 1, PositionChangeOrigin::Playback);

  /* Stop the audio output */
  player.stop();

  m_active_vgmcoll = nullptr;

  statusChange(false);
}

void SequencePlayer::seek(int position, PositionChangeOrigin origin) {
  player.seek(position);
  //  playbackPositionChanged(position, totalTicks(), origin);
  playbackPositionChanged(position, totalSamples(), origin);
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

  return loadCollection(coll, true);
}

bool SequencePlayer::setActiveCollection(const VGMColl *coll) {
  if (coll == m_active_vgmcoll) {
    return false;
  }

  const bool wasPlaying = playing();
  return loadCollection(coll, wasPlaying);
}

bool SequencePlayer::loadCollection(const VGMColl *coll, bool startPlaying) {

  VGMSeq *seq = coll->seq();
  if (!seq) {
    L_ERROR("Failed to play collection as it lacks sequence data.");
    return false;
  }

  if (!player.loadCollection(const_cast<VGMColl *>(coll), [this]() {
      QMetaObject::invokeMethod(this, "toggle", Qt::QueuedConnection);
    })) {
    L_ERROR("Could not load soundfont. Maybe the system is running out of "
                                  "memory or the sountfont was too large?");
    return false;
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
  m_song_title = QString::fromStdString(m_active_vgmcoll->name());
  if (startPlaying) {
    toggle();
  } else {
    player.pause();
    statusChange(false);
  }

  return true;
}

const VGMColl* SequencePlayer::activeCollection() const {
  return m_active_vgmcoll;
}
