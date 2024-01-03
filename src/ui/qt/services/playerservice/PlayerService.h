/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QObject>
#include <QTimer>
#include "VSTSequencePlayer.h"

class VGMColl;

class PlayerService : public QObject {
  Q_OBJECT
public:
  // Use the JUCE Singleton helpers as JUCE leak detection runs before static deconstructors and would therefore find
  // false positive memory leaks otherwise
  JUCE_DECLARE_SINGLETON(PlayerService, false )

  PlayerService(const PlayerService &) = delete;
  PlayerService &operator=(const PlayerService &) = delete;
  PlayerService(PlayerService &&) = delete;
  PlayerService &operator=(PlayerService &&) = delete;

  ~PlayerService();

  /**
   * Toggles the status of the player
   */
  Q_INVOKABLE void toggle();
  /**
   * Moves the player position
   * @param position relative to song start
   */
  /**
   * Stops the player (unloads resources)
   */
  void stop();
  void seek(int position);

  /**
   * Checks whether the player is playing
   * @return true if playing
   */
  [[nodiscard]] bool playing() const;
  /**
   * The number of samples elapsed since the player was started
   * @return number of ticks relative to song start
   */
  [[nodiscard]] int elapsedSamples() const;
  /**
   * The total number of samples in the song
   * @return total ticks in the song
   */
  [[nodiscard]] int totalSamples() const;

  /**
   * Returns the title of the song currently playing
   * @return the song title
   */
  [[nodiscard]] QString songTitle() const;

  /**
   * Loads a VGMColl for playback
   * @param collection
   * @return true if data was loaded correctly
   */
  bool playCollection(VGMColl *collection);

public slots:

signals:
  void statusChange(bool playing);
  void playbackPositionChanged(double current, double max);


private:
  PlayerService();

  VSTSequencePlayer player;

  VGMColl *m_active_vgmcoll{};

  QTimer *m_seekupdate_timer{};
  QString m_song_title{};
};
