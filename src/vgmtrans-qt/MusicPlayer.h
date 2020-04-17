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
#include <memory>

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
        fluid_settings_getstr_default(m_settings, "audio.driver", &def_driver);
#else
        def_driver = fluid_settings_getstr_default(settings, "audio.driver");
#endif

        return def_driver;
    }
    [[nodiscard]] std::vector<const char *> audioDrivers() const {
        std::vector<const char *> drivers_buf;
        fluid_settings_foreach_option(m_settings, "audio.driver", &drivers_buf,
#if FLUIDSYNTH_VERSION_MAJOR >= 2
                                      [](void *data, const char *, const char *option) {
#else
                                      [](void *data, char *, char *option) {
#endif
                                          auto drivers =
                                              static_cast<std::vector<const char *> *>(data);
                                          drivers->push_back(option);
                                      });

        return drivers_buf;
    }

    void updateSetting(const char *setting, int value);
    void updateSetting(const char *setting, const char *value);
    [[nodiscard]] bool checkSetting(const char *setting, const char *value) const;

   signals:
    void StatusChange(bool playing);

   private:
    void makeSettings();
    void makeSynth();

    fluid_settings_t *m_settings = nullptr;
    fluid_synth_t *m_synth = nullptr;
    fluid_audio_driver_t *m_active_driver = nullptr;
    fluid_player_t *m_active_player = nullptr;
    VGMColl *active_coll = nullptr;

    explicit MusicPlayer();
};

#if FLUIDSYNTH_VERSION_MAJOR >= 2

/*
 * A static wrapper for an SF2File object,
 * to implement FluidSynth's soundfont loading callbacks.
 * There's many checks which are probably redundant, but it's better to not trust external code.
 */
class SF2Wrapper {
   public:
    static const char *SetSF2(SF2File *obj) {
        s_old_sf2_buf = std::shared_ptr<const char[]>(static_cast<const char *>(obj->SaveToMem()));
        s_sf2_size = obj->GetSize();
        s_index = 0;

        return s_old_sf2_buf.get();
    }

    /* All the work was already done by SetSF2,
     * this is just sanity-checking to make sure the library is operating in the proper context */
    static void *sf_open(const char *data_buffer) {
        if (!data_buffer || data_buffer != s_old_sf2_buf.get()) {
            return nullptr;
        }

        return reinterpret_cast<void *>(const_cast<char *>(s_old_sf2_buf.get()));
    }

    static int sf_read(void *buf, int count, void *handle) {
        if (!buf || !handle || handle != s_old_sf2_buf.get() || count < 0 ||
            count + s_index >= s_sf2_size) {
            return FLUID_FAILED;
        }

        char *newhandle = static_cast<char *>(handle);
        newhandle += s_index;

        memcpy(buf, newhandle, count);

        s_index += count;

        return FLUID_OK;
    }

    static int sf_seek(void *handle, long offset, int origin) {
        if (!handle || handle != s_old_sf2_buf.get()) {
            return FLUID_FAILED;
        }

        switch (origin) {
            case SEEK_CUR: {
                if (s_index + offset > s_sf2_size || s_index + offset < 0) {
                    return FLUID_FAILED;
                }

                s_index += offset;
                break;
            }

            case SEEK_SET: {
                if (offset > s_sf2_size || offset < 0) {
                    return FLUID_FAILED;
                }

                s_index = offset;
                break;
            }

            case SEEK_END: {
                if (offset + s_sf2_size < 0 || offset > 0) {
                    return FLUID_FAILED;
                }

                s_index = s_sf2_size;
                break;
            }

            default: {
                return FLUID_FAILED;
            }
        }

        return FLUID_OK;
    }

    static long sf_tell(void *handle) {
        if (!handle || handle != s_old_sf2_buf.get()) {
            return FLUID_FAILED;
        }

        return s_index;
    }

    static int sf_close(void *handle) {
        if (!handle || handle != s_old_sf2_buf.get()) {
            return FLUID_FAILED;
        }

        s_old_sf2_buf.reset();
        s_index = 0;
        s_sf2_size = 0;
        return FLUID_OK;
    }

   private:
    inline static std::shared_ptr<const char[]> s_old_sf2_buf = nullptr;
    inline static long s_index = 0;
    inline static ssize_t s_sf2_size = 0;
};

#endif
