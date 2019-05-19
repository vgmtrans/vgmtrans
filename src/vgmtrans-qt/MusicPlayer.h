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
        fluid_settings_getstr_default(settings, "audio.driver", &def_driver);

        return def_driver;
    }
    const std::vector<const char *> audioDrivers() const {
        std::vector<const char *> drivers_buf;
        fluid_settings_foreach_option(settings, "audio.driver", &drivers_buf,
            [](void *data, const char *, const char *option) {
                auto drivers = reinterpret_cast<std::vector<const char *> *>(data);
                drivers->push_back(option);
            }
        );

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
        if (sf2_obj_) {
            delete sf2_obj_;
        }

        if (old_sf2_buf_) {
            delete static_cast<char *>(old_sf2_buf_);
            old_sf2_buf_ = nullptr;
        }

        sf2_obj_ = &obj;
        index_ = 0;
    }

    static void *sf_open(const char *filename) {
        void *sf2_buf;

        if (filename[0] != '&') {
            return nullptr;
        }

        sscanf(filename, "&%p", &sf2_buf);

        old_sf2_buf_ = sf2_buf;

        return sf2_buf;
    }

    static int sf_read(void *buf, int count, void *handle) {
        char *newhandle = (char *)handle;
        newhandle += index_;

        memcpy(buf, newhandle, count);

        index_ += count;

        return FLUID_OK;
    }

    static int sf_seek(void * /* handle */, long offset, int origin) {
        switch (origin) {
            case SEEK_CUR: {
                index_ += offset;
                break;
            }

            case SEEK_SET: {
                index_ = offset;
                break;
            }

            case SEEK_END: {
                index_ = sf2_obj_->GetSize();
                break;
            }

            default: {
                index_ = offset + origin;
                break;
            }
        }

        return FLUID_OK;
    }

    static long sf_tell(void *) { return index_; }

    /* Guaranteed to be called only on a SF2 buf */
    static int sf_close(void *file) {
        if (file == old_sf2_buf_ && old_sf2_buf_ != nullptr) {
            delete static_cast<char *>(old_sf2_buf_);
            old_sf2_buf_ = nullptr;
        }

        return FLUID_OK;
    }

   private:
    inline static SF2File *sf2_obj_ = nullptr;
    inline static void *old_sf2_buf_ = nullptr;
    inline static long index_ = 0;
};

#endif