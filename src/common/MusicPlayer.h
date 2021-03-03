/**
* VGMTrans (c) - 2002-2021
* Licensed under the zlib license
* See the included LICENSE for more information
*/

#pragma once

#include <string_view>
#include <vector>
#include <fluidsynth.h>
#include <gsl-lite.hpp>

class MusicPlayer {
public:
  MusicPlayer();
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
   * Loads SF2 and MIDI data into the player; resources are copied
   * @param soundfont_data
   * @param midi_data
   * @return true if data was loaded correctly
   */
  bool loadDataAndPlay(gsl::span<char> soundfont_data, gsl::span<char> midi_data);

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
  [[nodiscard]] std::vector<std::string_view> getAvailableDrivers() const;
  /**
   * Changes the current audio driver and restarts playing
   * @param driver_name
   * @return true if the driver changed
   */
  bool setAudioDriver(std::string_view driver_name);

private:
  fluid_settings_t *m_settings = nullptr;
  fluid_synth_t *m_synth = nullptr;
  fluid_audio_driver_t *m_active_driver = nullptr;
  fluid_player_t *m_active_player = nullptr;

  void makeSettings();
  void makeSynth();
  void makePlayer();
};
