/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QObject>
#include <QTimer>
#include "playback/NewSequencePlayer.h"

class VGMColl;

enum class PositionChangeOrigin {
  Playback,
  SeekBar,
  HexView
};

class SequencePlayer : public QObject {
  Q_OBJECT
public:
  // Use the JUCE Singleton helpers as JUCE leak detection runs before static deconstructors and would therefore find
  // false positive memory leaks otherwise
  JUCE_DECLARE_SINGLETON(SequencePlayer, false )

  SequencePlayer(const SequencePlayer &) = delete;
  SequencePlayer &operator=(const SequencePlayer &) = delete;
  SequencePlayer(SequencePlayer &&) = delete;
  SequencePlayer &operator=(SequencePlayer &&) = delete;

  ~SequencePlayer();

  /**
   * Toggles the status of the player
   */
  Q_INVOKABLE void toggle();

  /**
   * Stops the player (unloads resources)
   */
  void stop();
  /**
 * Moves the player position
 * @param position relative to song start
 */
  void seek(int position, PositionChangeOrigin origin);

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
   * Loads a VGMColl for playback and starts playing.
   * If the collection is already active, toggles play/pause.
   * @param collection
   * @return true if data was loaded correctly
   */
  bool playCollection(const VGMColl *collection);
  /**
   * Loads a VGMColl and makes it active without changing play/pause state.
   * @param collection
   * @return true if data was loaded correctly
   */
  bool setActiveCollection(const VGMColl *collection);
  [[nodiscard]] const VGMColl* activeCollection() const;

signals:
  void statusChange(bool playing);
  void playbackPositionChanged(int current, int max, PositionChangeOrigin origin);

private:
  SequencePlayer();
  bool loadCollection(const VGMColl *collection, bool startPlaying);

  NewSequencePlayer player;

  const VGMColl *m_active_vgmcoll{};

  QTimer *m_seekupdate_timer{};
  QString m_song_title{};
};
