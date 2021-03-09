/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "MusicPlayer.h"

/**
 * Wrapper for memory-loaded soundfonts that implements basic IO
 */
struct MemFileWrapper {
  static void *sf_open(const char *data_buffer) {
    if (!data_buffer) {
      return nullptr;
    }

    /* This is kinda cursed but we can't do much better */
    auto *data =
        new DataBlob{0, *reinterpret_cast<gsl::span<char> *>(const_cast<char *>(data_buffer))};

    return reinterpret_cast<void *>(data);
  }

  static int sf_read(void *buf, int count, void *handle) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!buf || !blob || count < 0 || count + blob->index >= static_cast<long>(blob->data.size())) {
      return FLUID_FAILED;
    }

    auto position = blob->data.begin() + blob->index;
    std::copy(position, position + count, static_cast<char *>(buf));

    blob->index += count;

    return FLUID_OK;
  }

  static int sf_seek(void *handle, long offset, int origin) {
    auto blob = static_cast<DataBlob *>(handle);
    if (!blob) {
      return FLUID_FAILED;
    }

    switch (origin) {
      case SEEK_CUR: {
        if (blob->index + offset > static_cast<long>(blob->data.size()) ||
            blob->index + offset < 0) {
          return FLUID_FAILED;
        }

        blob->index += offset;
        break;
      }

      case SEEK_SET: {
        if (offset > static_cast<long>(blob->data.size()) || offset < 0) {
          return FLUID_FAILED;
        }

        blob->index = offset;
        break;
      }

      case SEEK_END: {
        if (offset + static_cast<long>(blob->data.size()) < 0 || offset > 0) {
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

  static long sf_tell(void *handle) {
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
    long index{};
    gsl::span<char> data;
  };
};

MusicPlayer::MusicPlayer() {
  makeSettings();
  makeSynth();
}

MusicPlayer::~MusicPlayer() {
  if (m_active_player) {
    fluid_player_stop(m_active_player);
    delete_fluid_player(m_active_player);
    m_active_player = nullptr;
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
#ifdef __linux__
    /* Default to Pulseaudio on Linux */
    fluid_settings_setstr(m_settings, "audio.driver", "pulseaudio");
#elif defined(_WIN32)
    /* Default to DirectSound on Windows */
    fluid_settings_setstr(m_settings, "audio.driver", "waveout");
#endif
  }

  fluid_settings_setint(m_settings, "synth.reverb.active", 1);
  fluid_settings_setint(m_settings, "synth.chorus.active", 0);
  fluid_settings_setstr(m_settings, "synth.midi-bank-select", "mma");
  fluid_settings_setint(m_settings, "synth.midi-channels", 48);
}

void MusicPlayer::makeSynth() {
  if (m_active_player) {
    stop();
    delete_fluid_player(m_active_player);
  }

  if (m_synth) {
    delete_fluid_synth(m_synth);
  }

  if (m_active_driver) {
    delete_fluid_audio_driver(m_active_driver);
  }

  m_synth = new_fluid_synth(m_settings);

  fluid_sfloader_t *loader = new_fluid_defsfloader(m_settings);
  fluid_sfloader_set_callbacks(loader, MemFileWrapper::sf_open, &MemFileWrapper::sf_read,
                               &MemFileWrapper::sf_seek, &MemFileWrapper::sf_tell,
                               &MemFileWrapper::sf_close);
  fluid_synth_add_sfloader(m_synth, loader);
  m_active_driver = new_fluid_audio_driver(m_settings, m_synth);

  makePlayer();
}

void MusicPlayer::makePlayer() {
  if (m_active_player) {
    stop();
    delete_fluid_player(m_active_player);
    m_active_player = nullptr;
  }

  m_active_player = new_fluid_player(m_synth);
  fluid_player_stop(m_active_player);
}

bool MusicPlayer::playing() const {
  return m_active_player && fluid_player_get_status(m_active_player) == FLUID_PLAYER_PLAYING;
}

void MusicPlayer::seek(int position) {
  if (!m_active_player) {
    return;
  }

  if (fluid_player_seek(m_active_player, position) == FLUID_FAILED) {
    // todo: log
  }
}

bool MusicPlayer::toggle() {
  if (!m_active_player) {
    return false;
  }

  if (playing()) {
    fluid_player_stop(m_active_player);
  } else {
    fluid_player_play(m_active_player);
  }

  return playing();
}

void MusicPlayer::stop() {
  if (!m_active_player) {
    return;
  }

  fluid_player_stop(m_active_player);
}

bool MusicPlayer::loadDataAndPlay(gsl::span<char> soundfont_data, gsl::span<char> midi_data) {
  if (fluid_synth_sfload(m_synth, reinterpret_cast<char *>(&soundfont_data), 0) == FLUID_FAILED) {
    return false;
  }

  makePlayer();

  bool res = fluid_player_add_mem(m_active_player, midi_data.data(), midi_data.size()) == FLUID_OK;
  toggle();

  return res;
}

int MusicPlayer::elapsedTicks() const {
  if (!m_active_player) {
    return 0;
  }

  return fluid_player_get_current_tick(m_active_player);
}

int MusicPlayer::totalTicks() const {
  if (!m_active_player) {
    return 0;
  }

  return fluid_player_get_total_ticks(m_active_player);
}

std::vector<const char *> MusicPlayer::getAvailableDrivers() const {
  static std::vector<const char *> drivers_buf{};

  /* Availability of audio drivers shouldn't change over time, cache the result */
  if (drivers_buf.empty()) {
    fluid_settings_foreach_option(
        m_settings, "audio.driver", &drivers_buf, [](void *data, auto, auto option) {
          auto drivers = static_cast<std::vector<const char *> *>(data);
          drivers->push_back(option);
        });
  }

  return drivers_buf;
}

bool MusicPlayer::setAudioDriver(const char* driver_name) {
  if (fluid_settings_setstr(m_settings, "audio.driver", driver_name) == FLUID_FAILED) {
    return false;
  }

  auto position = elapsedTicks();
  auto was_playing = playing();
  makePlayer();

  seek(position);
  if (was_playing) {
    toggle();
  }

  return true;
}
