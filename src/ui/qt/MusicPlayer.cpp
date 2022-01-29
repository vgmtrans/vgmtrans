/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "MusicPlayer.h"

#include <QSettings>
#include <cstddef>
#include <memory>
#include <VGMColl.h>
#include <VGMSeq.h>
#include <MidiFile.h>
#include <SF2File.h>
#include <LogItem.h>
#include <fluidsynth/log.h>
#include "QtVGMRoot.h"

/**
 * Wrapper for memory-loaded soundfonts that implements basic IO
 */
struct MemFileWrapper {
#if (FLUIDSYNTH_VERSION_MAJOR == 2) && (FLUIDSYNTH_VERSION_MINOR <= 1)
  using size_type = long;
  using count_type = int;
#else
  using size_type = fluid_long_long_t;
  using count_type = fluid_long_long_t;
#endif

  static void *sf_open(const char *data_buffer) {
    if (!data_buffer) {
      return nullptr;
    }

    /* This is kinda cursed but we can't do much better */
    auto *data =
        new DataBlob{0, *reinterpret_cast<gsl::span<char> *>(const_cast<char *>(data_buffer))};

    return reinterpret_cast<void *>(data);
  }

  static int sf_read(void *buf, count_type count, void *handle) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!buf || !blob || count < 0 || count + blob->index >= static_cast<long>(blob->data.size())) {
      return FLUID_FAILED;
    }

    auto position = blob->data.begin() + blob->index;
    std::copy(position, position + count, static_cast<char *>(buf));

    blob->index += count;

    return FLUID_OK;
  }

  static int sf_seek(void *handle, size_type offset, int origin) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!blob) {
      return FLUID_FAILED;
    }

    switch (origin) {
      case SEEK_CUR: {
        if (blob->index + offset > static_cast<size_type>(blob->data.size()) ||
            blob->index + offset < 0) {
          return FLUID_FAILED;
        }

        blob->index += offset;
        break;
      }

      case SEEK_SET: {
        if (offset > static_cast<size_type>(blob->data.size()) || offset < 0) {
          return FLUID_FAILED;
        }

        blob->index = offset;
        break;
      }

      case SEEK_END: {
        if (offset + static_cast<size_type>(blob->data.size()) < 0 || offset > 0) {
          return FLUID_FAILED;
        }

        blob->index = blob->data.size();
        break;
      }

      default: {
        return FLUID_FAILED;
      }
    }

    return FLUID_OK;
  }

  static size_type sf_tell(void *handle) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!blob) {
      return FLUID_FAILED;
    }

    return blob->index;
  }

  static int sf_close(void *handle) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!blob) {
      return FLUID_FAILED;
    }

    delete blob;
    return FLUID_OK;
  }

private:
  struct DataBlob {
    size_type index{};
    gsl::span<char> data;
  };
};

static void fluidLogAdapter(int level, const char *message, void *data) {
  auto to_log = std::wstring{}.assign(message, strlen(message) + message);
  auto root = reinterpret_cast<QtVGMRoot *>(data);

  // FluidSynth's log levels are +1 compared to ours
  root->UI_AddLogItem(new LogItem(to_log, static_cast<LogLevel>(level--), L"FluidSynth"));
}

MusicPlayer::MusicPlayer() {
  QSettings settings;
  auto driver_option = settings.value("playback.audioDriver");
  if (driver_option.isNull()) {
#ifdef __linux__
    /* Default to Pulseaudio on Linux */
    settings.setValue("playback.audioDriver", "pulseaudio");
#elif defined(__APPLE__)
    /* Default to CoreAudio on macOS */
    settings.setValue("playback.audioDriver", "coreaudio");
#elif defined(_WIN32)
    /* Default to DirectSound on Windows */
    settings.setValue("playback.audioDriver", "dsound");
#endif
  }

  makeSettings();
  makeSynth();

  fluid_set_log_function(FLUID_PANIC, fluidLogAdapter, &qtVGMRoot);
  fluid_set_log_function(FLUID_ERR, fluidLogAdapter, &qtVGMRoot);
  fluid_set_log_function(FLUID_WARN, fluidLogAdapter, &qtVGMRoot);
  fluid_set_log_function(FLUID_INFO, fluidLogAdapter, &qtVGMRoot);
  fluid_set_log_function(FLUID_DBG, fluidLogAdapter, &qtVGMRoot);
}

MusicPlayer::~MusicPlayer() {
  if (m_sequencer) {
    m_sequencer = nullptr;
  }

  if (m_active_driver) {
    delete_fluid_audio_driver(m_active_driver);
    m_active_driver = nullptr;
  }
}

void MusicPlayer::makeSettings() {
  /* Create settings if needed */
  if (!m_settings) {
    m_settings = new_fluid_settings();

    QSettings settings;
    fluid_settings_setstr(m_settings, "audio.driver",
                          settings.value("playback.audioDriver").toString().toStdString().c_str());
  }

  fluid_settings_setint(m_settings, "synth.reverb.active", 1);
  fluid_settings_setint(m_settings, "synth.chorus.active", 0);
  fluid_settings_setint(m_settings, "synth.verbose", 1);
  fluid_settings_setstr(m_settings, "synth.midi-bank-select", "xg");
  fluid_settings_setint(m_settings, "synth.midi-channels", 256);
}

void MusicPlayer::makeSynth() {
  if (m_sequencer) {
    stop();
  }

  if (m_active_driver) {
    delete_fluid_audio_driver(m_active_driver);
    m_active_driver = nullptr;
  }

  if (m_synth) {
    delete_fluid_synth(m_synth);
    m_synth = nullptr;
  }

  m_synth = new_fluid_synth(m_settings);

  fluid_sfloader_t *loader = new_fluid_defsfloader(m_settings);
  fluid_sfloader_set_callbacks(loader, MemFileWrapper::sf_open, MemFileWrapper::sf_read,
                               MemFileWrapper::sf_seek, MemFileWrapper::sf_tell,
                               MemFileWrapper::sf_close);
  fluid_synth_add_sfloader(m_synth, loader);
  m_active_driver = new_fluid_audio_driver(m_settings, m_synth);
}

void MusicPlayer::makeSequencer() {
  if (m_sequencer) {
    stop();
  }

  m_sequencer.reset(new_fluid_sequencer2(false));
  /* This is the synth that will play the MIDI events */
  fluid_sequencer_register_fluidsynth(m_sequencer.get(), m_synth);

  /* This is the callback for the UI state (seekbar) */
  auto callback = [](unsigned int, fluid_event_t *, fluid_sequencer_t *, void *) {
    auto &mp = MusicPlayer::the();
    mp.playbackPositionChanged(mp.elapsedTicks(), mp.totalTicks());
  };
  fluid_sequencer_register_client(m_sequencer.get(), "ui_seekbar", callback, nullptr);
}

bool MusicPlayer::playing() const {
  return m_sequencer != nullptr && elapsedTicks() <= totalTicks();
}

void MusicPlayer::seek(int) {
  /* TODO: Reimplement */
  return;
}

bool MusicPlayer::toggle() {
  /* TODO: Implement pausing */
  if (!m_sequencer) {
    return false;
  }

  if (playing()) {
    stop();
  }

  statusChange(true);
  return true;
}

void MusicPlayer::stop() {
  if (!m_sequencer) {
    return;
  }

  fluid_synth_all_notes_off(m_synth, -1);
  m_active_coll = nullptr;
  m_sequencer = nullptr;
  statusChange(false);
}

QString MusicPlayer::songTitle() const {
  if (!m_active_coll) {
    return {};
  }

  return QString::fromStdWString(*m_active_coll->GetName());
}

bool MusicPlayer::playCollection(VGMColl *coll) {
  if (coll == m_active_coll) {
    toggle();
    return false;
  }

  /*todo: log the failures*/
  if (coll->sampcolls.empty()) {
    return false;
  }

  VGMSeq *seq = coll->GetSeq();
  if (!seq) {
    return false;
  }

  SF2File *sf2 = coll->CreateSF2File();
  if (!sf2) {
    return false;
  }

  auto rawSF2 = sf2->SaveToMem();
  delete sf2;

  auto soundfont_data = gsl::make_span(reinterpret_cast<char *>(rawSF2.data()), rawSF2.size());
  if (fluid_synth_sfload(m_synth, reinterpret_cast<char *>(&soundfont_data), 0) == FLUID_FAILED) {
    return false;
  }

  makeSequencer();

  auto midi = std::unique_ptr<MidiFile>(seq->ConvertToMidi());
  processMidiFile(std::move(midi));

  statusChange(true);
  m_active_coll = coll;

  return true;
}

int MusicPlayer::elapsedTicks() const {
  if (!m_sequencer) {
    return 0;
  }

  return fluid_sequencer_get_tick(m_sequencer.get());
}

int MusicPlayer::totalTicks() const {
  if (!m_sequencer) {
    return 0;
  }

  return m_total_ticks;
}

std::vector<const char *> MusicPlayer::getAvailableDrivers() const {
  static std::vector<const char *> drivers_buf{};

  /* Availability of audio drivers can't change over time, cache the result */
  if (drivers_buf.empty()) {
    fluid_settings_foreach_option(m_settings, "audio.driver", &drivers_buf,
                                  [](void *data, auto, auto option) {
                                    auto drivers = static_cast<std::vector<const char *> *>(data);
                                    drivers->push_back(option);
                                  });
  }

  return drivers_buf;
}

bool MusicPlayer::setAudioDriver(const char *driver_name) {
  if (fluid_settings_setstr(m_settings, "audio.driver", driver_name) == FLUID_FAILED) {
    return false;
  }

  QSettings settings;
  settings.setValue("playback.audioDriver", driver_name);

  qtVGMRoot.UI_AddLogItem(
      new LogItem(QString("Switched playback backend to \"%1\"").arg(driver_name).toStdWString(),
                  LOG_LEVEL_INFO, L"MusicPlayer"));
  makeSynth();

  return true;
}

bool MusicPlayer::checkSetting(const char *setting, const char *value) const {
  return fluid_settings_str_equal(m_settings, setting, value) != 0;
}

void MusicPlayer::updateSetting(const char *setting, const char *value) {
  fluid_settings_setstr(m_settings, setting, value);
  makeSynth();
}

void MusicPlayer::processMidiFile(std::unique_ptr<MidiFile> midi) {
  /* Try to speed up allocation of midi events */
  std::vector<MidiEvent *> events;
  events.reserve(midi->aTracks.size() *
                 std::accumulate(std::begin(midi->aTracks), std::end(midi->aTracks), 0,
                                 [](int sum, MidiTrack *t) { return sum + t->aEvents.size(); }));

  for (auto &track : midi->aTracks) {
    events.insert(std::end(events), std::begin(track->aEvents), std::end(track->aEvents));
  }

  midi->globalTranspose = 0;
  events.insert(std::end(events), std::begin(midi->globalTrack.aEvents),
                std::end(midi->globalTrack.aEvents));

  if (events.empty()) {
    /* Kind of unusual but.. it happens */
    return;
  }

  /* We want events in their absolute time order, sorted by priority */
  std::stable_sort(std::begin(events), std::end(events), PriorityCmp());
  std::stable_sort(std::begin(events), std::end(events), AbsTimeCmp());

  /* Default to 120 BPM */
  auto ppqn = midi->GetPPQN();
  fluid_sequencer_set_time_scale(m_sequencer.get(), 1000000.0 * (ppqn / 500000.0));

  auto time_marker = fluid_sequencer_get_tick(m_sequencer.get());
  auto seqid = fluid_sequencer_get_client_id(m_sequencer.get(), 0);
  for (auto &event : events) {
    fluid_event_t *seq_event = new_fluid_event();
    fluid_event_set_source(seq_event, -1);
    fluid_event_set_dest(seq_event, seqid);

    /* Channel 9 is drum only, we want to avoid using it */
    int channel = event->channel + ((event->channel == 9 + event->prntTrk->channelGroup) * 16);
    switch (event->GetEventType()) {
      case MIDIEVENT_NOTEON: {
        auto note = dynamic_cast<NoteEvent *>(event);
        /* We don't have MIDIEVENT_NOTEOFF... */
        if (note->bNoteDown) {
          fluid_event_noteon(seq_event, channel, note->key, note->vel);
        } else {
          fluid_event_noteoff(seq_event, channel, note->key);
        }
        break;
      }

      case MIDIEVENT_BANKSELECT: {
        auto bank_sel = dynamic_cast<BankSelectEvent *>(event);
        fluid_event_bank_select(seq_event, channel, bank_sel->dataByte);
        break;
      }

      case MIDIEVENT_PROGRAMCHANGE: {
        auto prog_change = dynamic_cast<ProgChangeEvent *>(event);
        fluid_event_program_change(seq_event, channel, prog_change->programNum);
        break;
      }

      case MIDIEVENT_TEMPO: {
        auto tempo_change = dynamic_cast<TempoEvent *>(event);
        fluid_event_scale(seq_event, 1000000.0 * double(ppqn) / tempo_change->microSecs);
        break;
      }

      case MIDIEVENT_VOLUME: {
        auto volume = dynamic_cast<VolumeEvent *>(event);
        fluid_event_volume(seq_event, channel, volume->dataByte);
        break;
      }

      case MIDIEVENT_PAN: {
        auto pan = dynamic_cast<PanEvent *>(event);
        fluid_event_pan(seq_event, channel, pan->dataByte);
        break;
      }

      case MIDIEVENT_MODULATION: {
        auto modulation = dynamic_cast<ModulationEvent *>(event);
        fluid_event_modulation(seq_event, channel, modulation->dataByte);
        break;
      }

      case MIDIEVENT_SUSTAIN: {
        auto sustain = dynamic_cast<SustainEvent *>(event);
        fluid_event_sustain(seq_event, channel, sustain->dataByte);
        break;
      }

      case MIDIEVENT_PITCHBEND: {
        auto pitch_bend = dynamic_cast<PitchBendEvent *>(event);
        /* Bend values are 14bit, 0 is at 8192 */
        fluid_event_pitch_bend(seq_event, channel, std::clamp(pitch_bend->bend + 8192, 0, 16383));
        break;
      }

      case MIDIEVENT_RESET: {
        fluid_event_system_reset(seq_event);
        break;
      }

      /* Used for controller events */
      case MIDIEVENT_UNDEFINED: {
        auto cc = dynamic_cast<ControllerEvent *>(event);
        fluid_event_control_change(seq_event, channel, cc->controlNum, cc->dataByte);
        break;
      }

      /* Default covers all MIDI events, the program only uses a subset */
      default:
      /* These 3 events are not useful when listening to the track */
      case MIDIEVENT_ENDOFTRACK:
      case MIDIEVENT_MASTERVOL:
      case MIDIEVENT_TEXT: {
        delete_fluid_event(seq_event);
        continue;
      }
    }

    /* Send the event to the synth for playing */
    fluid_sequencer_send_at(m_sequencer.get(), seq_event, time_marker + event->AbsTime, true);

    /* Send the event to the UI callback */
    fluid_event_set_dest(seq_event, fluid_sequencer_get_client_id(m_sequencer.get(), 1));
    fluid_sequencer_send_at(m_sequencer.get(), seq_event, time_marker + event->AbsTime, true);

    delete_fluid_event(seq_event);
  }

  /* Give an estimate for the total ticks of the sequence */
  m_total_ticks = time_marker + events.back()->AbsTime;
}
