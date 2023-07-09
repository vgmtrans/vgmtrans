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
#include "SF2Conversion.h"
#include "QtVGMRoot.h"

/* How often (in ms) the current ticks are polled */
static constexpr auto TICK_POLL_INTERVAL_MS = 1000/60;

SequencePlayer::SequencePlayer() {
  player.initialize();

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      // playbackPositionChanged(elapsedTicks(), totalTicks(), PositionChangeOrigin::Playback);
      playbackPositionChanged(elapsedSamples(), totalSamples());
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

  SF2File *sf2 = conversion::createSF2File(*coll);
  if (!sf2) {
    L_ERROR("Failed to play collection as a soundfont file could not be produced.");
    return false;
  }

  auto rawSF2 = sf2->saveToMem();
  delete sf2;
  /* Deleted by MemFile::mem_close */
//  auto sf2_data_blob = new MemFile::DataBlob{0, std::vector<uint8_t>(rawSF2)};

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
