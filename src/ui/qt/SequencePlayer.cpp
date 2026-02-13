/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "SequencePlayer.h"
#include <algorithm>
#include <cstddef>

#include "bass.h"
#include "bassmidi.h"
#include "VGMColl.h"
#include "SF2File.h"
#include "VGMSeq.h"
#include "LogManager.h"
#include "SF2Conversion.h"
#include "QtVGMRoot.h"

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
static constexpr auto TICK_POLL_INTERVAL_MS = 1000/60;
static constexpr float PREVIEW_BUFFER_SECONDS = 0.1f;
static constexpr QWORD INVALID_MIDI_TICK = static_cast<QWORD>(-1);

static BOOL CALLBACK suppressPreviewPlaybackEvents(HSTREAM,
                                                   int,
                                                   BASS_MIDI_EVENT*,
                                                   BOOL seeking,
                                                   void*) {
  // Keep all events while seeking so channel/program/controller state is reconstructed.
  if (seeking) {
    return true;
  }
  // Drop all timeline-driven events during playback; preview notes are injected manually.
  return false;
}

SequencePlayer::SequencePlayer() {
  /* Use the system default output device.
   * The sample rate is actually only respected on Linux: on Windows and macOS, BASS will use the
   * native sampling frequency. */
  BASS_Init(-1, 44100, 0, nullptr, nullptr);
  if (BASS_ErrorGetCode() != BASS_OK) {
    L_ERROR("Failed to initialize audio device. Collection playback will not be available.");
    return;
  }

  m_seekupdate_timer = new QTimer(this);
  connect(m_seekupdate_timer, &QTimer::timeout, [this]() {
    if (playing()) {
      playbackPositionChanged(elapsedTicks(), totalTicks(), PositionChangeOrigin::Playback);
    }
  });
  m_seekupdate_timer->start(TICK_POLL_INTERVAL_MS);

  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMColl, this, [this](VGMColl* coll) {
    if (m_active_vgmcoll == coll) {
      stop();
    }
  });
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
  releasePreviewStream();

  /* Stop polling seekbar, reset it, propagate that we're done */
  playbackPositionChanged(0, 1, PositionChangeOrigin::Playback);

  /* Stop the audio output */
  BASS_ChannelStop(m_active_stream);

  /* Release resources */
  BASS_MIDI_FontFree(m_loaded_sf);
  m_loaded_sf = 0;
  BASS_StreamFree(m_active_stream);
  m_active_stream = 0;
  m_active_midi_data.clear();
  m_active_vgmcoll = nullptr;

  statusChange(false);
}

void SequencePlayer::seek(int position, PositionChangeOrigin origin) {
  stopPreviewNote();
  BASS_ChannelSetPosition(m_active_stream, position, BASS_POS_MIDI_TICK);
  playbackPositionChanged(position, totalTicks(), origin);
}

bool SequencePlayer::previewNoteOn(uint8_t channel, uint8_t key, uint8_t velocity) {
  if (!m_active_stream || !ensurePreviewStream()) {
    return false;
  }

  stopPreviewNote();

  const QWORD activeTick = BASS_ChannelGetPosition(m_active_stream, BASS_POS_MIDI_TICK);
  if (activeTick == INVALID_MIDI_TICK) {
    return false;
  }
  if (!BASS_ChannelSetPosition(m_preview_stream, activeTick, BASS_POS_MIDI_TICK)) {
    return false;
  }

  float volume = 1.0f;
  if (BASS_ChannelGetAttribute(m_active_stream, BASS_ATTRIB_VOL, &volume)) {
    BASS_ChannelSetAttribute(m_preview_stream, BASS_ATTRIB_VOL, volume);
  }
  BASS_ChannelPlay(m_preview_stream, false);

  const DWORD packed = static_cast<DWORD>(key) | (static_cast<DWORD>(velocity) << 8);
  if (!BASS_MIDI_StreamEvent(m_preview_stream, channel, MIDI_EVENT_NOTE, packed)) {
    return false;
  }

  m_previewNoteActive = true;
  m_previewNoteChannel = channel;
  m_previewNoteKey = key;
  return true;
}

void SequencePlayer::stopPreviewNote() {
  if (!m_previewNoteActive) {
    return;
  }

  if (m_preview_stream) {
    // NOTE event with velocity 0 is interpreted as Note Off for this key.
    const DWORD noteOffParam = static_cast<DWORD>(m_previewNoteKey);
    BASS_MIDI_StreamEvent(m_preview_stream, m_previewNoteChannel, MIDI_EVENT_NOTE, noteOffParam);
    BASS_ChannelPause(m_preview_stream);
  }

  m_previewNoteActive = false;
  m_previewNoteChannel = 0;
  m_previewNoteKey = 0;
}

bool SequencePlayer::ensurePreviewStream() {
  if (m_preview_stream) {
    return true;
  }
  if (!m_loaded_sf || m_active_midi_data.empty()) {
    return false;
  }

  HSTREAM previewStream = BASS_MIDI_StreamCreateFile(
      true, m_active_midi_data.data(), 0, m_active_midi_data.size(), BASS_MIDI_DECAYEND, 0);
  if (!previewStream) {
    return false;
  }

  BASS_MIDI_FONT font;
  font.font = m_loaded_sf;
  font.preset = -1;
  font.bank = 0;
  if (!BASS_MIDI_StreamSetFonts(previewStream, &font, 1)) {
    BASS_StreamFree(previewStream);
    return false;
  }

  BASS_ChannelSetAttribute(previewStream, BASS_ATTRIB_MIDI_CHANS, 128);
  BASS_ChannelFlags(previewStream, 0, BASS_MIDI_NOFX);
  BASS_ChannelSetAttribute(previewStream, BASS_ATTRIB_BUFFER, PREVIEW_BUFFER_SECONDS);
  if (!BASS_MIDI_StreamSetFilter(previewStream, false, suppressPreviewPlaybackEvents, nullptr)) {
    BASS_StreamFree(previewStream);
    return false;
  }

  m_preview_stream = previewStream;
  return true;
}

void SequencePlayer::releasePreviewStream() {
  stopPreviewNote();

  if (!m_preview_stream) {
    return;
  }

  BASS_StreamFree(m_preview_stream);
  m_preview_stream = 0;
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
  auto sf2_data_blob = new MemFile::DataBlob{0, std::move(rawSF2)};

  /* Init soundfont */
  HSOUNDFONT sf2_handle =
      BASS_MIDI_FontInitUser(&memory_file_callbacks, sf2_data_blob, BASS_MIDI_FONT_XGDRUMS);
  if (BASS_ErrorGetCode() != BASS_OK) {
    L_ERROR("Could not load soundfont. Maybe the system is running out of "
                                  "memory or the sountfont was too large?");
    return false;
  }

  MidiFile *midi = seq->convertToMidi(coll);
  std::vector<uint8_t> raw_midi;
  midi->writeMidiToBuffer(raw_midi);
  /* Set up the MIDI stream */
  HSTREAM midi_stream =
      BASS_MIDI_StreamCreateFile(true, raw_midi.data(), 0, raw_midi.size(), BASS_MIDI_DECAYEND, 0);
  if (BASS_ErrorGetCode() != BASS_OK) {
    BASS_MIDI_FontFree(sf2_handle);

    L_ERROR("Failed reading MIDI data");
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

    L_ERROR("Could not assign soundfont to MIDI data");
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
  m_active_midi_data = std::move(raw_midi);
  m_song_title = QString::fromStdString(m_active_vgmcoll->name());
  if (startPlaying) {
    toggle();
  } else {
    BASS_ChannelPause(m_active_stream);
    statusChange(false);
  }

  return true;
}

const VGMColl* SequencePlayer::activeCollection() const {
  return m_active_vgmcoll;
}
