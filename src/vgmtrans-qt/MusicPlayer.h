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

#ifdef _MSC_VER
#ifndef _SSIZE_T_DEFINED
#undef ssize_t
#include <BaseTsd.h>
typedef _W64 SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif

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
    ~MusicPlayer() override;

    bool SynthPlaying();
    void LoadCollection(VGMColl *coll);
    void Toggle();
    void Stop();
    void Seek(int ticks);

    [[nodiscard]] std::vector<const char *> audioDrivers() const {
        std::vector<const char *> drivers_buf;
        fluid_settings_foreach_option(m_settings, "audio.driver", &drivers_buf,
                                      [](void *data, auto, auto option) {
                                          auto drivers =
                                              static_cast<std::vector<const char *> *>(data);
                                          drivers->push_back(option);
                                      });

        return drivers_buf;
    }

    [[maybe_unused]] void updateSetting(const char *setting, int value);
    void updateSetting(const char *setting, const char *value);
    [[nodiscard]] bool checkSetting(const char *setting, const char *value) const;

    /* This thing intercepts all MIDI events to keep the UI slider scrolling.
     * There is really no other sane way of doing this.
     * Periodically querying the status with a timer is not an option as the total number of ticks
     * isn't reported in a reliable way unless you're 100% sure the player is playing a file.
     * Which you can't be unless you're processing one, since the player can be in the playing state while actually
     * playing back nothing */
    static int PlayerCallback(void *data, fluid_midi_event_t *event) {
        auto fdata = static_cast<std::pair<fluid_player_t *, fluid_synth_t *>*>(data);
        auto player = fdata->first;
        emit MusicPlayer::Instance().PositionChanged(fluid_player_get_current_tick(player), fluid_player_get_total_ticks(player));

        auto synth = fdata->second;
        return fluid_synth_handle_midi_event(synth, event);
    }

   signals:
    void StatusChange(bool playing);
    void PositionChanged(int cur, int max);

   private:
    void makeSettings();
    void makeSynth();
    void makePlayer();

    fluid_settings_t *m_settings = nullptr;
    fluid_synth_t *m_synth = nullptr;
    fluid_audio_driver_t *m_active_driver = nullptr;
    fluid_player_t *m_active_player = nullptr;
    VGMColl *active_coll = nullptr;

    MusicPlayer();
};

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
