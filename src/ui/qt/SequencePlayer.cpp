/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "SequencePlayer.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

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
static constexpr float PREVIEW_BUFFER_SECONDS = 0.15f;
static constexpr DWORD INVALID_MIDI_EVENT_VALUE = static_cast<DWORD>(-1);
static constexpr DWORD DEFAULT_MIDI_MASTER_VOLUME = 0x3FFF;
static constexpr DWORD DEFAULT_MIDI_TEMPO_USEC_PER_QUARTER = 500000;
static constexpr DWORD DEFAULT_MIDI_PAN = 64;
static constexpr DWORD DEFAULT_MIDI_VOLUME = 100;
static constexpr std::array<DWORD, 31> PREVIEW_CHANNEL_STATE_EVENTS = {
    MIDI_EVENT_BANK,      MIDI_EVENT_BANK_LSB,  MIDI_EVENT_DRUMS,      MIDI_EVENT_PROGRAM,
    MIDI_EVENT_MODULATION, MIDI_EVENT_MODRANGE, MIDI_EVENT_VOLUME,     MIDI_EVENT_PAN,
    MIDI_EVENT_EXPRESSION, MIDI_EVENT_PITCHRANGE, MIDI_EVENT_PITCH,    MIDI_EVENT_CHANPRES,
    MIDI_EVENT_SUSTAIN,   MIDI_EVENT_SOSTENUTO, MIDI_EVENT_SOFT,       MIDI_EVENT_PORTAMENTO,
    MIDI_EVENT_PORTATIME, MIDI_EVENT_PORTANOTE, MIDI_EVENT_REVERB,     MIDI_EVENT_CHORUS,
    MIDI_EVENT_CUTOFF,    MIDI_EVENT_RESONANCE, MIDI_EVENT_ATTACK,     MIDI_EVENT_DECAY,
    MIDI_EVENT_RELEASE,   MIDI_EVENT_FINETUNE,  MIDI_EVENT_COARSETUNE, MIDI_EVENT_VIBRATO_RATE,
    MIDI_EVENT_VIBRATO_DEPTH, MIDI_EVENT_VIBRATO_DELAY, MIDI_EVENT_MOD_PITCH};

static uint16_t previewNoteChannelAndKey(const SequencePlayer::PreviewNote& note) {
  return static_cast<uint16_t>((static_cast<uint16_t>(note.channel) << 8) | note.key);
}

static void sendPreviewNoteOff(HSTREAM stream, const SequencePlayer::PreviewNote& note) {
  if (!stream) {
    return;
  }
  // NOTE event with velocity 0 is interpreted as Note Off for this key.
  const DWORD noteOffParam = static_cast<DWORD>(note.key);
  BASS_MIDI_StreamEvent(stream, note.channel, MIDI_EVENT_NOTE, noteOffParam);
}

static std::vector<SequencePlayer::PreviewNote> sanitizePreviewNotes(
    const std::vector<SequencePlayer::PreviewNote>& notes) {
  std::vector<SequencePlayer::PreviewNote> validNotes;
  validNotes.reserve(notes.size());

  std::unordered_set<uint16_t> seenChannelAndKeys;
  seenChannelAndKeys.reserve(notes.size() * 2 + 1);
  for (const auto& note : notes) {
    if (note.channel >= 128 || note.key >= 128 || note.velocity == 0) {
      continue;
    }

    if (!seenChannelAndKeys.emplace(previewNoteChannelAndKey(note)).second) {
      continue;
    }
    validNotes.push_back(note);
  }

  return validNotes;
}

static DWORD bpmToTempoUsec(double bpm) {
  if (bpm <= 0.0) {
    return DEFAULT_MIDI_TEMPO_USEC_PER_QUARTER;
  }

  const double usecPerQuarter = 60000000.0 / bpm;
  const auto clamped = static_cast<DWORD>(std::clamp<double>(usecPerQuarter, 1.0, 16777215.0));
  return clamped;
}

static double tempoUsecToBpm(DWORD usecPerQuarter) {
  if (usecPerQuarter == 0) {
    return 120.0;
  }
  return 60000000.0 / static_cast<double>(usecPerQuarter);
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
  m_seekupdate_timer->setTimerType(Qt::PreciseTimer);
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
    m_playbackStateByCollection.erase(coll);
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
  releasePreviewStreams();

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
  if (!(origin == PositionChangeOrigin::PianoRoll && !playing())) {
    stopPreviewNote();
  }
  BASS_ChannelSetPosition(m_active_stream, position, BASS_POS_MIDI_TICK);
  playbackPositionChanged(position, totalTicks(), origin);
}

bool SequencePlayer::setTempoBpm(double bpm) {
  if (!m_active_stream) {
    return false;
  }

  return sendStreamEvent(0, MIDI_EVENT_TEMPO, bpmToTempoUsec(bpm));
}

bool SequencePlayer::setChannelPan(uint8_t channel, uint8_t pan) {
  if (channel >= 128 || !m_active_stream) {
    return false;
  }

  return sendStreamEvent(channel, MIDI_EVENT_PAN, std::clamp<DWORD>(pan, 0, 127), true);
}

bool SequencePlayer::setChannelVolume(uint8_t channel, uint8_t volume) {
  if (channel >= 128 || !m_active_stream) {
    return false;
  }

  return sendStreamEvent(channel, MIDI_EVENT_VOLUME, std::clamp<DWORD>(volume, 0, 127), true);
}

bool SequencePlayer::setChannelMixLevel(uint8_t channel, uint8_t percent) {
  if (channel >= 128 || !m_active_stream) {
    return false;
  }

  const DWORD mixLevel = std::clamp<DWORD>(percent, 0, 200);
  const bool sent = sendStreamEvent(channel, MIDI_EVENT_MIXLEVEL, mixLevel, true);
  if (!sent || mixLevel > 0) {
    return sent;
  }

  // Immediately clear currently sounding notes when muting.
  sendStreamEvent(channel, MIDI_EVENT_NOTESOFF, 0, true);
  sendStreamEvent(channel, MIDI_EVENT_SOUNDOFF, 0, true);
  return true;
}

double SequencePlayer::currentTempoBpm() const {
  DWORD tempoUsec = streamEventValue(0, MIDI_EVENT_TEMPO);
  if (tempoUsec == INVALID_MIDI_EVENT_VALUE) {
    tempoUsec = DEFAULT_MIDI_TEMPO_USEC_PER_QUARTER;
  }
  return tempoUsecToBpm(tempoUsec);
}

int SequencePlayer::currentChannelPan(uint8_t channel) const {
  if (channel >= 128) {
    return static_cast<int>(DEFAULT_MIDI_PAN);
  }

  DWORD pan = streamEventValue(channel, MIDI_EVENT_PAN);
  if (pan == INVALID_MIDI_EVENT_VALUE) {
    pan = DEFAULT_MIDI_PAN;
  }
  return static_cast<int>(std::clamp<DWORD>(pan, 0, 127));
}

int SequencePlayer::currentChannelVolume(uint8_t channel) const {
  if (channel >= 128) {
    return static_cast<int>(DEFAULT_MIDI_VOLUME);
  }

  DWORD volume = streamEventValue(channel, MIDI_EVENT_VOLUME);
  if (volume == INVALID_MIDI_EVENT_VALUE) {
    volume = DEFAULT_MIDI_VOLUME;
  }
  return static_cast<int>(std::clamp<DWORD>(volume, 0, 127));
}

bool SequencePlayer::channelMuted(const VGMColl* collection, int channelId) const {
  if (!collection || channelId < 0) {
    return false;
  }

  const PlaybackState* state = findPlaybackState(collection);
  if (!state || static_cast<size_t>(channelId) >= state->mutedChannels.size()) {
    return false;
  }
  return state->mutedChannels[static_cast<size_t>(channelId)] != 0;
}

bool SequencePlayer::channelSolo(const VGMColl* collection, int channelId) const {
  if (!collection || channelId < 0) {
    return false;
  }

  const PlaybackState* state = findPlaybackState(collection);
  if (!state || static_cast<size_t>(channelId) >= state->soloChannels.size()) {
    return false;
  }
  return state->soloChannels[static_cast<size_t>(channelId)] != 0;
}

void SequencePlayer::setChannelMuted(const VGMColl* collection,
                                     int channelId,
                                     bool muted,
                                     int channelCount) {
  if (!collection || channelId < 0) {
    return;
  }

  PlaybackState& state = ensurePlaybackState(collection, channelCount);
  const size_t index = static_cast<size_t>(channelId);
  if (index >= state.mutedChannels.size()) {
    state.mutedChannels.resize(index + 1, 0);
    state.soloChannels.resize(index + 1, 0);
  }

  state.mutedChannels[index] = static_cast<uint8_t>(muted ? 1 : 0);
  if (muted) {
    state.soloChannels[index] = 0;
  }
}

void SequencePlayer::setChannelSolo(const VGMColl* collection,
                                    int channelId,
                                    bool solo,
                                    int channelCount) {
  if (!collection || channelId < 0) {
    return;
  }

  PlaybackState& state = ensurePlaybackState(collection, channelCount);
  const size_t index = static_cast<size_t>(channelId);
  if (index >= state.soloChannels.size()) {
    state.mutedChannels.resize(index + 1, 0);
    state.soloChannels.resize(index + 1, 0);
  }

  if (state.mutedChannels[index] != 0 && solo) {
    return;
  }
  state.soloChannels[index] = static_cast<uint8_t>(solo ? 1 : 0);
}

// Computes effective enabled state from saved mute/solo toggles.
std::vector<uint8_t> SequencePlayer::channelEnabledMask(const VGMColl* collection,
                                                        int channelCount) const {
  const size_t count = static_cast<size_t>(std::max(0, channelCount));
  std::vector<uint8_t> enabledMask(count, static_cast<uint8_t>(1));
  const PlaybackState* state = findPlaybackState(collection);
  if (!state) {
    return enabledMask;
  }

  bool anySolo = false;
  for (size_t i = 0; i < count; ++i) {
    if (i < state->soloChannels.size() && state->soloChannels[i] != 0) {
      anySolo = true;
      break;
    }
  }

  for (size_t i = 0; i < count; ++i) {
    const bool muted = i < state->mutedChannels.size() && state->mutedChannels[i] != 0;
    const bool soloed = i < state->soloChannels.size() && state->soloChannels[i] != 0;
    enabledMask[i] = static_cast<uint8_t>((!muted && (!anySolo || soloed)) ? 1 : 0);
  }

  return enabledMask;
}

bool SequencePlayer::previewNoteOn(uint8_t channel, uint8_t key, uint8_t velocity, uint32_t tick) {
  return previewNotesAtTick({PreviewNote{channel, key, velocity}}, tick);
}

bool SequencePlayer::previewNotesAtTick(const std::vector<PreviewNote>& notes, uint32_t tick) {
  if (!m_active_stream || !ensurePreviewStreams()) {
    return false;
  }

  stopPreviewNote();
  std::vector<PreviewNote> validNotes = sanitizePreviewNotes(notes);
  if (validNotes.empty()) {
    return notes.empty();
  }

  // Snap the decode-only state stream once, then copy state into the live preview stream.
  if (!syncPreviewStateAtTick(tick)) {
    return false;
  }
  syncPreviewGlobalState();

  std::array<bool, 128> syncedChannels{};
  for (const auto& note : validNotes) {
    if (syncedChannels[note.channel]) {
      continue;
    }
    if (!syncPreviewChannelState(note.channel)) {
      return false;
    }
    syncedChannels[note.channel] = true;
  }
  BASS_ChannelPlay(m_preview_note_stream, false);

  m_previewActiveNotes.clear();
  m_previewActiveNotes.reserve(validNotes.size());
  for (const auto& note : validNotes) {
    const DWORD packed = static_cast<DWORD>(note.key) | (static_cast<DWORD>(note.velocity) << 8);
    if (!BASS_MIDI_StreamEvent(m_preview_note_stream, note.channel, MIDI_EVENT_NOTE, packed)) {
      continue;
    }
    m_previewActiveNotes.push_back(note);
  }

  return !m_previewActiveNotes.empty();
}

bool SequencePlayer::updatePreviewNotesAtTick(const std::vector<PreviewNote>& notes, uint32_t tick) {
  if (!m_active_stream || !ensurePreviewStreams()) {
    return false;
  }

  std::vector<PreviewNote> targetNotes = sanitizePreviewNotes(notes);
  if (targetNotes.empty()) {
    stopPreviewNote();
    return true;
  }

  std::unordered_map<uint16_t, PreviewNote> targetByKey;
  targetByKey.reserve(targetNotes.size() * 2 + 1);
  for (const auto& note : targetNotes) {
    targetByKey.emplace(previewNoteChannelAndKey(note), note);
  }

  std::vector<PreviewNote> notesToStop;
  notesToStop.reserve(m_previewActiveNotes.size());
  std::vector<PreviewNote> keptNotes;
  keptNotes.reserve(std::min(m_previewActiveNotes.size(), targetNotes.size()));
  std::unordered_set<uint16_t> keptKeys;
  keptKeys.reserve(targetNotes.size() * 2 + 1);
  std::array<bool, 128> channelHasKeptNotes{};

  for (const auto& activeNote : m_previewActiveNotes) {
    const uint16_t key = previewNoteChannelAndKey(activeNote);
    if (targetByKey.find(key) == targetByKey.end()) {
      notesToStop.push_back(activeNote);
      continue;
    }
    keptNotes.push_back(activeNote);
    keptKeys.emplace(key);
    channelHasKeptNotes[activeNote.channel] = true;
  }

  std::vector<PreviewNote> notesToStart;
  notesToStart.reserve(targetNotes.size());
  for (const auto& note : targetNotes) {
    if (keptKeys.find(previewNoteChannelAndKey(note)) != keptKeys.end()) {
      continue;
    }
    notesToStart.push_back(note);
  }

  for (const auto& note : notesToStop) {
    sendPreviewNoteOff(m_preview_note_stream, note);
  }

  if (!syncPreviewStateAtTick(tick)) {
    m_previewActiveNotes = std::move(keptNotes);
    return false;
  }
  syncPreviewGlobalState();

  std::array<bool, 128> syncedChannels{};
  for (const auto& note : targetNotes) {
    if (syncedChannels[note.channel]) {
      continue;
    }
    const bool resetChannel = !channelHasKeptNotes[note.channel];
    if (!syncPreviewChannelState(note.channel, resetChannel)) {
      m_previewActiveNotes = std::move(keptNotes);
      return false;
    }
    syncedChannels[note.channel] = true;
  }
  BASS_ChannelPlay(m_preview_note_stream, false);

  std::unordered_set<uint16_t> startedKeys;
  startedKeys.reserve(notesToStart.size() * 2 + 1);
  for (const auto& note : notesToStart) {
    const DWORD packed = static_cast<DWORD>(note.key) | (static_cast<DWORD>(note.velocity) << 8);
    if (!BASS_MIDI_StreamEvent(m_preview_note_stream, note.channel, MIDI_EVENT_NOTE, packed)) {
      continue;
    }
    startedKeys.emplace(previewNoteChannelAndKey(note));
  }

  m_previewActiveNotes.clear();
  m_previewActiveNotes.reserve(targetNotes.size());
  for (const auto& note : targetNotes) {
    const uint16_t key = previewNoteChannelAndKey(note);
    if (keptKeys.find(key) != keptKeys.end() || startedKeys.find(key) != startedKeys.end()) {
      m_previewActiveNotes.push_back(note);
    }
  }

  return !m_previewActiveNotes.empty();
}

void SequencePlayer::stopPreviewNote() {
  if (m_previewActiveNotes.empty()) {
    return;
  }

  if (m_preview_note_stream) {
    for (const auto& note : m_previewActiveNotes) {
      sendPreviewNoteOff(m_preview_note_stream, note);
    }
  }

  m_previewActiveNotes.clear();
}

bool SequencePlayer::sendStreamEvent(uint8_t channel, DWORD event, DWORD value, bool toPreviewStream) {
  if (!m_active_stream) {
    return false;
  }

  const bool sentToActive = BASS_MIDI_StreamEvent(m_active_stream, channel, event, value) != FALSE;
  if (toPreviewStream && m_preview_note_stream) {
    BASS_MIDI_StreamEvent(m_preview_note_stream, channel, event, value);
  }
  return sentToActive;
}

DWORD SequencePlayer::streamEventValue(uint8_t channel, DWORD event) const {
  if (!m_active_stream) {
    return INVALID_MIDI_EVENT_VALUE;
  }
  return BASS_MIDI_StreamGetEvent(m_active_stream, channel, event);
}

SequencePlayer::PlaybackState& SequencePlayer::ensurePlaybackState(const VGMColl* collection, int channelCount) {
  PlaybackState& state = m_playbackStateByCollection[collection];
  const size_t count = static_cast<size_t>(std::max(0, channelCount));
  if (state.mutedChannels.size() < count) {
    state.mutedChannels.resize(count, 0);
  }
  if (state.soloChannels.size() < count) {
    state.soloChannels.resize(count, 0);
  }
  return state;
}

const SequencePlayer::PlaybackState* SequencePlayer::findPlaybackState(const VGMColl* collection) const {
  if (!collection) {
    return nullptr;
  }

  const auto it = m_playbackStateByCollection.find(collection);
  if (it == m_playbackStateByCollection.end()) {
    return nullptr;
  }
  return &it->second;
}

bool SequencePlayer::ensurePreviewStreams() {
  if (m_preview_note_stream && m_preview_state_stream) {
    return true;
  }
  if (!m_loaded_sf || m_active_midi_data.empty()) {
    return false;
  }

  auto bindSoundfont = [this](HSTREAM stream) -> bool {
    BASS_MIDI_FONT font;
    font.font = m_loaded_sf;
    font.preset = -1;
    font.bank = 0;
    return BASS_MIDI_StreamSetFonts(stream, &font, 1);
  };

  if (!m_preview_note_stream) {
    HSTREAM previewNoteStream = BASS_MIDI_StreamCreate(128, BASS_MIDI_DECAYEND, 0);
    if (!previewNoteStream) {
      return false;
    }
    if (!bindSoundfont(previewNoteStream)) {
      BASS_StreamFree(previewNoteStream);
      return false;
    }
    BASS_ChannelSetAttribute(previewNoteStream, BASS_ATTRIB_MIDI_CHANS, 128);
    BASS_ChannelFlags(previewNoteStream, 0, BASS_MIDI_NOFX);
    BASS_ChannelSetAttribute(previewNoteStream, BASS_ATTRIB_BUFFER, PREVIEW_BUFFER_SECONDS);
    BASS_ChannelPlay(previewNoteStream, false);
    m_preview_note_stream = previewNoteStream;
  }

  if (!m_preview_state_stream) {
    HSTREAM previewStateStream = BASS_MIDI_StreamCreateFile(
        true,
        m_active_midi_data.data(),
        0,
        m_active_midi_data.size(),
        BASS_MIDI_DECAYEND | BASS_STREAM_DECODE,
        0);
    if (!previewStateStream) {
      if (m_preview_note_stream) {
        BASS_StreamFree(m_preview_note_stream);
        m_preview_note_stream = 0;
      }
      return false;
    }
    if (!bindSoundfont(previewStateStream)) {
      BASS_StreamFree(previewStateStream);
      if (m_preview_note_stream) {
        BASS_StreamFree(m_preview_note_stream);
        m_preview_note_stream = 0;
      }
      return false;
    }
    BASS_ChannelSetAttribute(previewStateStream, BASS_ATTRIB_MIDI_CHANS, 128);
    BASS_ChannelFlags(previewStateStream, 0, BASS_MIDI_NOFX);
    m_preview_state_stream = previewStateStream;
  }

  return true;
}

void SequencePlayer::releasePreviewStreams() {
  stopPreviewNote();

  if (m_preview_note_stream) {
    BASS_StreamFree(m_preview_note_stream);
    m_preview_note_stream = 0;
  }

  if (m_preview_state_stream) {
    BASS_StreamFree(m_preview_state_stream);
    m_preview_state_stream = 0;
  }
}

bool SequencePlayer::syncPreviewStateAtTick(uint32_t tick) {
  if (!m_preview_note_stream || !m_preview_state_stream) {
    return false;
  }

  // BASS_POS_MIDI_TICK positions to the state *before* that tick, so seek one tick ahead to
  // include controller/program updates that happen at the same tick as the selected note.
  QWORD seekTick = static_cast<QWORD>(tick) + 1;
  const QWORD stateLength = BASS_ChannelGetLength(m_preview_state_stream, BASS_POS_MIDI_TICK);
  if (stateLength != static_cast<QWORD>(-1) && seekTick > stateLength) {
    seekTick = stateLength;
  }

  if (!BASS_ChannelSetPosition(m_preview_state_stream, seekTick, BASS_POS_MIDI_TICK) &&
      !BASS_ChannelSetPosition(m_preview_state_stream, tick, BASS_POS_MIDI_TICK)) {
    return false;
  }

  return true;
}

void SequencePlayer::syncPreviewGlobalState() {
  // Apply current global output gain so preview loudness matches playback.
  float volume = 1.0f;
  if (BASS_ChannelGetAttribute(m_active_stream, BASS_ATTRIB_VOL, &volume)) {
    BASS_ChannelSetAttribute(m_preview_note_stream, BASS_ATTRIB_VOL, volume);
  }

  DWORD masterVolume = BASS_MIDI_StreamGetEvent(m_preview_state_stream, 0, MIDI_EVENT_MASTERVOL);
  if (masterVolume == INVALID_MIDI_EVENT_VALUE) {
    masterVolume = DEFAULT_MIDI_MASTER_VOLUME;
  }
  BASS_MIDI_StreamEvent(m_preview_note_stream, 0, MIDI_EVENT_MASTERVOL, masterVolume);
}

bool SequencePlayer::syncPreviewChannelState(uint8_t channel, bool resetChannel) {
  if (!m_preview_note_stream || !m_preview_state_stream) {
    return false;
  }

  if (resetChannel) {
    // Clear previous preview state on this channel so missing events fall back to defaults.
    BASS_MIDI_StreamEvent(m_preview_note_stream, channel, MIDI_EVENT_RESET, 0);
    BASS_MIDI_StreamEvent(m_preview_note_stream, channel, MIDI_EVENT_SOUNDOFF, 0);
  } else {
    BASS_MIDI_StreamEvent(m_preview_note_stream, channel, MIDI_EVENT_RESET, 0);
  }

  for (const DWORD eventType : PREVIEW_CHANNEL_STATE_EVENTS) {
    const DWORD param = BASS_MIDI_StreamGetEvent(m_preview_state_stream, channel, eventType);
    if (param == INVALID_MIDI_EVENT_VALUE) {
      continue;
    }
    BASS_MIDI_StreamEvent(m_preview_note_stream, channel, eventType, param);
  }

  return true;
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
  auto sf2_data_blob = std::make_unique<MemFile::DataBlob>(MemFile::DataBlob{0, std::move(rawSF2)});

  /* Init soundfont */
  HSOUNDFONT sf2_handle =
      BASS_MIDI_FontInitUser(&memory_file_callbacks, sf2_data_blob.get(), BASS_MIDI_FONT_XGDRUMS);
  if (BASS_ErrorGetCode() != BASS_OK) {
    L_ERROR("Could not load soundfont. Maybe the system is running out of "
                                  "memory or the sountfont was too large?");
    return false;
  }
  sf2_data_blob.release();

  auto midi = std::unique_ptr<MidiFile>(seq->convertToMidi(coll));
  if (!midi) {
    BASS_MIDI_FontFree(sf2_handle);
    L_ERROR("Failed to convert sequence to MIDI");
    return false;
  }
  std::vector<uint8_t> raw_midi;
  midi->writeMidiToBuffer(raw_midi);
  /* Set up the MIDI stream */
  HSTREAM midi_stream =
      BASS_MIDI_StreamCreateFile(true, raw_midi.data(), 0, raw_midi.size(), BASS_MIDI_DECAYEND, 0);
  if (BASS_ErrorGetCode() != BASS_OK) {
    BASS_MIDI_FontFree(sf2_handle);

    L_ERROR("Failed to read MIDI data");
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
