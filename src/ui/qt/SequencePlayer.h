/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <cstdint>
#include <vector>

#include <bass.h>
#include <bassmidi.h>
#include <QObject>
#include <QTimer>

class VGMColl;

enum class PositionChangeOrigin {
  Playback,
  SeekBar,
  HexView
};

class SequencePlayer : public QObject {
  Q_OBJECT
public:
  struct PreviewNote {
    uint8_t channel = 0;
    uint8_t key = 0;
    uint8_t velocity = 0;
  };

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
  void seek(int position, PositionChangeOrigin origin);
  bool previewNoteOn(uint8_t channel, uint8_t key, uint8_t velocity, uint32_t tick);
  bool previewNotesAtTick(const std::vector<PreviewNote>& notes, uint32_t tick);
  void stopPreviewNote();

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
  bool ensurePreviewStreams();
  void releasePreviewStreams();
  bool syncPreviewStateAtTick(uint32_t tick);
  void syncPreviewGlobalState();
  bool syncPreviewChannelState(uint8_t channel);

  const VGMColl *m_active_vgmcoll{};
  HSTREAM m_active_stream{};
  HSTREAM m_preview_note_stream{};
  HSTREAM m_preview_state_stream{};
  HSOUNDFONT m_loaded_sf{};
  std::vector<uint8_t> m_active_midi_data;

  QTimer *m_seekupdate_timer{};
  QString m_song_title{};
  std::vector<PreviewNote> m_previewActiveNotes;
};
