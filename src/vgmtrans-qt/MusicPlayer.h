/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstring>
#include <QObject>
#include <VGMColl.h>
#include <SF2File.h>

extern "C" {
#include <fluidsynth.h>
}

class MusicPlayer : public QObject {
    Q_OBJECT

   public:
    MusicPlayer(const MusicPlayer &) = delete;
    MusicPlayer &operator=(const MusicPlayer &) = delete;
    MusicPlayer(MusicPlayer &&) = delete;
    MusicPlayer &operator=(MusicPlayer &&) = delete;

    static MusicPlayer &Instance();
    ~MusicPlayer();

    bool SynthPlaying();
    void LoadCollection(VGMColl *coll);
    void Toggle();
    void Stop();

    const char *defaultAudioDriver() {
        char *def_driver;
#if FLUIDSYNTH_VERSION_MAJOR >= 2
        fluid_settings_getstr_default(settings, "audio.driver", &def_driver);
#else
        def_driver = fluid_settings_getstr_default(settings, "audio.driver");
#endif

        return def_driver;
    }
    const std::vector<const char *> audioDrivers() const {
        std::vector<const char *> drivers_buf;
        fluid_settings_foreach_option(settings, "audio.driver", &drivers_buf,
#if FLUIDSYNTH_VERSION_MAJOR >= 2
                                      [](void *data, const char *, const char *option) {
#else
                                      [](void *data, char *, char *option) {
#endif
                                          auto drivers =
                                              reinterpret_cast<std::vector<const char *> *>(data);
                                          drivers->push_back(option);
                                      });

        return drivers_buf;
    }

    void updateSetting(const char *setting, int value);
    void updateSetting(const char *setting, const char *value);
    bool checkSetting(const char *setting, const char *value);

   signals:
    void StatusChange(bool playing);

   private:
    void makeSettings();
    void makeSynth();

    fluid_settings_t *settings = nullptr;
    fluid_synth_t *synth = nullptr;
    fluid_audio_driver_t *adriver = nullptr;
    fluid_player_t *player = nullptr;
    int sfont_id;
    VGMColl *active_coll = nullptr;

    inline static std::vector<const char *> m_drivers;

    explicit MusicPlayer();
};

#if FLUIDSYNTH_VERSION_MAJOR >= 2

/*
 * This class represents a static wrapper to get around
 * C pointer-to-function requirements and implements
 * IO routines to allow fluidsynth to read soundfonts
 * from memory.
 */

class SF2Wrapper {
   public:
    static void SetSF2(SF2File &obj) {
        if (m_sf2_obj) {
            delete m_sf2_obj;
        }

        if (m_old_sf2_buf) {
            delete[] static_cast<char *>(m_old_sf2_buf);
            m_old_sf2_buf = nullptr;
        }

        m_sf2_obj = &obj;
        m_index = 0;
    }

    static void *sf_open(const char *filename) {
        void *sf2_buf;

        if (filename[0] != '&') {
            return nullptr;
        }

        sscanf(filename, "&%p", &sf2_buf);

        m_old_sf2_buf = sf2_buf;

        return sf2_buf;
    }

    static int sf_read(void *buf, int count, void *handle) {
        char *newhandle = (char *)handle;
        newhandle += m_index;

        memcpy(buf, newhandle, count);

        m_index += count;

        return FLUID_OK;
    }

    static int sf_seek(void * /* handle */, long offset, int origin) {
        switch (origin) {
            case SEEK_CUR: {
                m_index += offset;
                break;
            }

            case SEEK_SET: {
                m_index = offset;
                break;
            }

            case SEEK_END: {
                m_index = m_sf2_obj->GetSize();
                break;
            }

            default: {
                m_index = offset + origin;
                break;
            }
        }

        return FLUID_OK;
    }

    static long sf_tell(void *) { return m_index; }

    /* Guaranteed to be called only on a SF2 buf */
    static int sf_close(void *file) {
        if (file == m_old_sf2_buf && m_old_sf2_buf != nullptr) {
            delete[] static_cast<char *>(m_old_sf2_buf);
            m_old_sf2_buf = nullptr;
        }

        return FLUID_OK;
    }

   private:
    inline static SF2File *m_sf2_obj = nullptr;
    inline static void *m_old_sf2_buf = nullptr;
    inline static long m_index = 0;
};

#endif