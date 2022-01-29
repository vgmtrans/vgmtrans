/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <vector>
#include <fluidsynth.h>
#include <gsl-lite.hpp>
#include <QObject>
#include <QString>

class VGMColl;
class MidiFile;

class MusicPlayer : public QObject {
  Q_OBJECT
public:
  static auto &the() {
    static MusicPlayer instance;
    return instance;
  }

  MusicPlayer(const MusicPlayer &) = delete;
  MusicPlayer &operator=(const MusicPlayer &) = delete;
  MusicPlayer(MusicPlayer &&) = delete;
  MusicPlayer &operator=(MusicPlayer &&) = delete;

  ~MusicPlayer();

  /**
   * Toggles the status of the player
   * @returns true if the player is playing
   */
  bool toggle();
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
   * Gets all the audio driver the player supports
   * @return the driver list
   */
  [[nodiscard]] std::vector<const char *> getAvailableDrivers() const;
  /**
   * Changes the current audio driver and restarts playing
   * @param driver_name
   * @return true if the driver changed
   */
  bool setAudioDriver(const char *driver_name);

  /**
   * Checks the value of a setting of the player corresponds to the passed value
   * @param setting
   * @param value
   * @return true if the value matches
   */
  [[nodiscard]] bool checkSetting(const char *setting, const char *value) const;
  /**
   * Sets a parameter of the player to the corrisponding passed value
   * @param setting
   * @param value
   */
  void updateSetting(const char *setting, const char *value);

signals:
  void statusChange(bool playing);
  void playbackPositionChanged(int current, int max);

private:
  MusicPlayer();

  static void sequencerDeleter(fluid_sequencer_t *seq) {
    fluid_sequencer_unregister_client(seq, fluid_sequencer_get_client_id(seq, 0));
    fluid_sequencer_unregister_client(seq, fluid_sequencer_get_client_id(seq, 1));
    delete_fluid_sequencer(seq);
  }

  fluid_settings_t *m_settings = nullptr;
  fluid_synth_t *m_synth = nullptr;
  fluid_audio_driver_t *m_active_driver = nullptr;
  std::unique_ptr<fluid_sequencer_t, decltype(&sequencerDeleter)> m_sequencer{nullptr,
                                                                             sequencerDeleter};
  int m_total_ticks = 0;
  VGMColl *m_active_coll = nullptr;

  /**
   * @brief Creates settings for the synth.
   */
  void makeSettings();

  /**
   * @brief Spawns a new synth.
   */
  void makeSynth();

  /**
   * @brief Spawns a new sequencer, replacing the old instance.
   *
   * It automatically binds to the MusicPlayer's synth, hence it MUST be called after makeSynth()
   */
  void makeSequencer();

  /**
   * @brief Places MIDI events from a MIDI file onto the sequencer, essentially implementing
   * playback
   *
   * @param midi
   */
  void processMidiFile(std::unique_ptr<MidiFile> midi);
};
