/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "SequencePlayer.h"
#include <algorithm>
#include <cstddef>

#include "Root.h"
#include "bass.h"
#include "bassmidi.h"
#include "VGMColl.h"
#include "SF2File.h"
#include "VGMSeq.h"

/**
 * @brief Routines to read file data from memory.
 */
namespace MemFile {
struct DataBlob {
  QWORD index{};
  std::vector<uint8_t> data;
};

static DWORD mem_read(void *buf, DWORD count, void *handle) {
  auto blob = static_cast<DataBlob *>(handle);
  if (!buf || !blob || count < 0) {
    return 0;
  }

  /* This function never fails: count is the *maximum* number of bytes the caller wants, not a
   * precise number of bytes to fetch */
  count = std::clamp(count, DWORD(0), DWORD(blob->data.size() - blob->index));

  auto position = blob->data.begin() + blob->index;
  std::copy(position, position + count, static_cast<char *>(buf));

  blob->index += count;

  return count;
}

static BOOL mem_seek(QWORD offset, void *handle) {
  auto blob = static_cast<DataBlob *>(handle);
  if (!blob) {
    return false;
  }

  blob->index = offset;

  return true;
}

static QWORD mem_tell(void *handle) {
  auto blob = static_cast<DataBlob *>(handle);
  if (!blob) {
    return 0;
  }

  return blob->data.size();
}

static void mem_close(void *handle) {
  auto blob = static_cast<DataBlob *>(handle);
  if (!blob) {
    return;
  }

  delete blob;
}
};  // namespace MemFile

static constexpr BASS_FILEPROCS memory_file_callbacks{MemFile::mem_close, MemFile::mem_tell,
                                                      MemFile::mem_read, MemFile::mem_seek};
/* How often (in ms) the current ticks are polled */
static constexpr auto TICK_POLL_INTERVAL_MS = 10;

SequencePlayer::SequencePlayer() {
  /* Use the system default output device.
   * The sample rate is actually only respected on Linux: on Windows and macOS, BASS will use the
   * native sampling frequency. */
  BASS_Init(-1, 44100, 0, nullptr, nullptr);
  if (BASS_ErrorGetCode() != BASS_OK) {
    pRoot->AddLogItem(new LogItem(
        "Failed to initialize audio device. Collection playback will not be available.",
        LOG_LEVEL_ERR, "SequencePlayer"));
    return;
  }

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      playbackPositionChanged(elapsedTicks(), totalTicks());
    }
  });
  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);
}

SequencePlayer::~SequencePlayer() {
  stop();
  BASS_Free();
}

void SequencePlayer::toggle() {
  if (playing()) {
    BASS_ChannelPause(m_active_stream);
    m_seekupdate_timer->stop();
  } else {
    BASS_ChannelPlay(m_active_stream, false);
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
  BASS_ChannelStop(m_active_stream);

  /* Release resources */
  BASS_MIDI_FontFree(m_loaded_sf);
  m_loaded_sf = 0;
  BASS_StreamFree(m_active_stream);
  m_active_stream = 0;
  m_active_vgmcoll = nullptr;
}

void SequencePlayer::seek(int position) {
  BASS_ChannelSetPosition(m_active_stream, position, BASS_POS_MIDI_TICK);
  playbackPositionChanged(position, totalTicks());
}

bool SequencePlayer::playing() const {
  return m_active_stream && BASS_ChannelIsActive(m_active_stream) == BASS_ACTIVE_PLAYING;
}

int SequencePlayer::elapsedTicks() const {
  return BASS_ChannelGetPosition(m_active_stream, BASS_POS_MIDI_TICK);
}

int SequencePlayer::totalTicks() const {
  return BASS_ChannelGetLength(m_active_stream, BASS_POS_MIDI_TICK);
}

QString SequencePlayer::songTitle() const {
  return m_song_title;
}

bool SequencePlayer::playCollection(VGMColl *coll) {
  if (coll == m_active_vgmcoll) {
    toggle();
    return false;
  }

  VGMSeq *seq = coll->GetSeq();
  if (!seq) {
    pRoot->AddLogItem(new LogItem("Failed to play collection as it lacks sequence data.",
                                  LOG_LEVEL_ERR, "SequencePlayer"));
    return false;
  }

  SF2File *sf2 = coll->CreateSF2File();
  if (!sf2) {
    pRoot->AddLogItem(
        new LogItem("Failed to play collection as a soundfont file could not be produced.",
                    LOG_LEVEL_ERR, "SequencePlayer"));
    return false;
  }

  auto rawSF2 = sf2->SaveToMem();
  /* Deleted by MemFile::mem_close */
  auto sf2_data_blob = new MemFile::DataBlob{0, std::move(rawSF2)};

  /* Init soundfont */
  HSOUNDFONT sf2_handle =
      BASS_MIDI_FontInitUser(&memory_file_callbacks, sf2_data_blob, BASS_MIDI_FONT_XGDRUMS);
  if (BASS_ErrorGetCode() != BASS_OK) {
    pRoot->AddLogItem(new LogItem("Could not load soundfont. Maybe the system is running out of "
                                  "memory or the sountfont was too large?",
                                  LOG_LEVEL_ERR, "SequencePlayer"));
    return false;
  }

  MidiFile *midi = seq->ConvertToMidi();
  std::vector<uint8_t> raw_midi;
  midi->WriteMidiToBuffer(raw_midi);
  /* Set up the MIDI stream */
  HSTREAM midi_stream =
      BASS_MIDI_StreamCreateFile(true, raw_midi.data(), 0, raw_midi.size(), BASS_MIDI_DECAYEND, 0);
  if (BASS_ErrorGetCode() != BASS_OK) {
    BASS_MIDI_FontFree(sf2_handle);

    pRoot->AddLogItem(new LogItem("Failed reading MIDI data", LOG_LEVEL_ERR, "SequencePlayer"));
    return false;
  }

  /* Tie the loaded soundfont to the new MIDI stream */
  BASS_MIDI_FONT font;
  font.font = sf2_handle;
  font.preset = -1;
  font.bank = 0;
  BASS_MIDI_StreamSetFonts(midi_stream, &font, 1);
  if (BASS_ErrorGetCode() != BASS_OK) {
    BASS_MIDI_FontFree(sf2_handle);
    BASS_StreamFree(midi_stream);

    pRoot->AddLogItem(
        new LogItem("Could not assign soundfont to MIDI data", LOG_LEVEL_ERR, "SequencePlayer"));
    return false;
  }

  /* Use 128 MIDI channels */
  BASS_ChannelSetAttribute(midi_stream, BASS_ATTRIB_MIDI_CHANS, 128);
  /* Turn on FXs (reverb, chorus) */
  BASS_ChannelFlags(midi_stream, 0, BASS_MIDI_NOFX);

  /* Set callback used to signal that the playback is over */
  static auto stop_callback = [](HSYNC, DWORD, DWORD, void *player) {
    reinterpret_cast<SequencePlayer *>(player)->stop();
  };
  BASS_ChannelSetSync(midi_stream, BASS_SYNC_END, 0, stop_callback, this);

  /* Stop the current playback in order to start the new song */
  stop();

  /* Reassign tracking info and start playback */
  m_active_vgmcoll = coll;
  m_active_stream = midi_stream;
  m_loaded_sf = sf2_handle;
  m_song_title = QString::fromStdString(*m_active_vgmcoll->GetName());
  toggle();

  return true;
}
