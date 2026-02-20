/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "PlayerService.h"

#include "VGMColl.h"
#include "SF2File.h"
#include "VGMSeq.h"
#include "LogManager.h"
#include "SF2Conversion.h"
#include "QtVGMRoot.h"

/* How often (in ms) the current ticks are polled */
static constexpr auto TICK_POLL_INTERVAL_MS = 1000/60;

JUCE_IMPLEMENT_SINGLETON (PlayerService)

PlayerService::PlayerService() {
  player.initialize();

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      const int currentSamples = elapsedSamples();
      const int maxSamples = totalSamples();
      const int currentTicks = elapsedTicks();
      const int maxTicks = totalTicks();
      playbackSamplePositionChanged(currentSamples, maxSamples, PositionChangeOrigin::Playback);
      playbackTickPositionChanged(currentTicks, maxTicks, PositionChangeOrigin::Playback);
      playbackPositionChanged(currentSamples, maxSamples, PositionChangeOrigin::Playback);
    }
  });
  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);

  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, [this](VGMColl* coll) {
    if (m_active_vgmcoll == coll) {
      stop();
    }
  });
}

PlayerService::~PlayerService() {
  clearSingletonInstance();
}

void PlayerService::toggle() {
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

void PlayerService::stop() {
  /* Stop polling seekbar, reset it, propagate that we're done */
  playbackSamplePositionChanged(0, 1, PositionChangeOrigin::Playback);
  playbackTickPositionChanged(0, 1, PositionChangeOrigin::Playback);
  playbackPositionChanged(0, 1, PositionChangeOrigin::Playback);

  /* Stop the audio output */
  player.stop();

  m_active_vgmcoll = nullptr;

  statusChange(false);
}

void PlayerService::seekSamples(int position, PositionChangeOrigin origin) {
  player.seek(position);
  const int currentSamples = elapsedSamples();
  const int maxSamples = totalSamples();
  const int currentTicks = elapsedTicks();
  const int maxTicks = totalTicks();
  playbackSamplePositionChanged(currentSamples, maxSamples, origin);
  playbackTickPositionChanged(currentTicks, maxTicks, origin);
  playbackPositionChanged(currentSamples, maxSamples, origin);
}

void PlayerService::seekTicks(int position, PositionChangeOrigin origin) {
  seekSamples(player.samplesFromTicks(position), origin);
}

void PlayerService::seek(int position, PositionChangeOrigin origin) {
  seekSamples(position, origin);
}

bool PlayerService::playing() const {
  return player.isPlaying();
}

int PlayerService::elapsedSamples() const {
  return static_cast<int>(player.elapsedSamples());
}

int PlayerService::totalSamples() const {
    return static_cast<int>(player.totalSamples());
}

int PlayerService::elapsedTicks() const {
  return static_cast<int>(player.elapsedTicks());
}

int PlayerService::totalTicks() const {
  return static_cast<int>(player.totalTicks());
}

QString PlayerService::songTitle() const {
  return m_song_title;
}

bool PlayerService::playCollection(const VGMColl *coll) {
  if (coll == m_active_vgmcoll) {
    toggle();
    return false;
  }

  return loadCollection(coll, true);
}

bool PlayerService::setActiveCollection(const VGMColl *coll) {
  if (coll == m_active_vgmcoll) {
    return false;
  }

  const bool wasPlaying = playing();
  return loadCollection(coll, wasPlaying);
}

bool PlayerService::loadCollection(const VGMColl *coll, bool startPlaying) {

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
  m_song_title = QString::fromStdString(m_active_vgmcoll->name());
//  toggle();
  if (startPlaying) {
    toggle();
  } else {
    player.pause();
    statusChange(false);
  }

  return true;
}

const VGMColl* PlayerService::activeCollection() const {
  return m_active_vgmcoll;
}
