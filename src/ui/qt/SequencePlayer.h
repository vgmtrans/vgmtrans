/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <bass.h>
#include <bassmidi.h>
#include <QObject>
#include <QTimer>

class VGMColl;

class SequencePlayer : public QObject {
  Q_OBJECT
public:
  static auto &the() {
    static SequencePlayer instance;
    return instance;
  }

  SequencePlayer(const SequencePlayer &) = delete;
  SequencePlayer &operator=(const SequencePlayer &) = delete;
  SequencePlayer(SequencePlayer &&) = delete;
  SequencePlayer &operator=(SequencePlayer &&) = delete;

  ~SequencePlayer();

  /**
   * Toggles the status of the player
   */
  void toggle();
  /**
   * Stops the player (unloads resources)
   */
  void stop();
  /**
   * Moves the player position
   * @param position relative to song start
   */
  void seek(int position);

  /**
   * Checks whether the player is playing
   * @return true if playing
   */
  [[nodiscard]] bool playing() const;
  /**
   * The number of MIDI ticks elapsed since the player was started
   * @return number of ticks relative to song start
   */
  [[nodiscard]] int elapsedTicks() const;
  /**
   * The total number of MIDI ticks in the song
   * @return total ticks in the song
   */
  [[nodiscard]] int totalTicks() const;

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

signals:
  void statusChange(bool playing);
  void playbackPositionChanged(int current, int max);

private:
  SequencePlayer();

  VGMColl *m_active_vgmcoll{};
  HSTREAM m_active_stream{};
  HSOUNDFONT m_loaded_sf{};

  QTimer *m_seekupdate_timer{};
};
