/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MusicPlayer.h"

#include <QTemporaryDir>
#include <VGMSeq.h>
#include <LogManager.h>

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

MusicPlayer &MusicPlayer::Instance() {
    static MusicPlayer gui_player;
    return gui_player;
}

void MusicPlayer::makeSettings() {
    /* Create settings if needed */
    if (!m_settings) {
        m_settings = new_fluid_settings();
/* Default to Pulseaudio on Linux */
#ifdef __linux__
        fluid_settings_setstr(m_settings, "audio.driver", "pulseaudio");
#endif
    }

    fluid_settings_setint(m_settings, "synth.reverb.active", 1);
    fluid_settings_setint(m_settings, "synth.chorus.active", 0);
    fluid_settings_setstr(m_settings, "synth.midi-bank-select", "mma");
    fluid_settings_setint(m_settings, "synth.midi-channels", 48);
}

void MusicPlayer::makeSynth() {
    if (m_active_player) {
        Stop();
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
    fluid_sfloader_set_callbacks(loader, &SF2Wrapper::sf_open, &SF2Wrapper::sf_read,
                                 &SF2Wrapper::sf_seek, &SF2Wrapper::sf_tell, &SF2Wrapper::sf_close);
    fluid_synth_add_sfloader(m_synth, loader);
    m_active_driver = new_fluid_audio_driver(m_settings, m_synth);

    makePlayer();
}

void MusicPlayer::makePlayer() {
    static std::pair<fluid_player_t *, fluid_synth_t *> data = {nullptr, nullptr};
    if (m_active_player) {
        Stop();
        delete_fluid_player(m_active_player);
        m_active_player = nullptr;
    }

    m_active_player = new_fluid_player(m_synth);
    data = {m_active_player, m_synth};
    fluid_player_set_playback_callback(m_active_player, MusicPlayer::PlayerCallback, &data);
    fluid_player_stop(m_active_player);
}

[[maybe_unused]] void MusicPlayer::updateSetting(const char *setting, int value) {
    fluid_settings_setint(m_settings, setting, value);
    makeSynth();
}

void MusicPlayer::updateSetting(const char *setting, const char *value) {
    fluid_settings_setstr(m_settings, setting, value);
    makeSynth();
}

bool MusicPlayer::checkSetting(const char *setting, const char *value) const {
    return fluid_settings_str_equal(m_settings, setting, value) != 0;
}

void MusicPlayer::Toggle() {
    if (!m_active_player) {
        return;
    }

    auto status = SynthPlaying();
    if (status) {
        fluid_player_stop(m_active_player);
    } else {
        fluid_player_play(m_active_player);
    }

    emit StatusChange(SynthPlaying());
}

void MusicPlayer::Stop() {
    fluid_player_stop(m_active_player);
    active_coll = nullptr;

    emit StatusChange(false);
}

void MusicPlayer::Seek(int position) {
    if (!m_active_player) {
        return;
    }

    if (fluid_player_seek(m_active_player, position) == FLUID_FAILED) {
        L_ERROR("Failed seeking to {}", position);
    }
}

bool MusicPlayer::SynthPlaying() {
    return m_active_player && fluid_player_get_status(m_active_player) == FLUID_PLAYER_PLAYING;
}

void MusicPlayer::LoadCollection(VGMColl *coll) {
    /* Don't reload the same collection - allows pausing */
    if (active_coll == coll) {
        return;
    }

    VGMSeq *seq = coll->GetSeq();
    if (!seq) {
        L_ERROR("There's no sequence to play for '{}'", coll->GetName());
        return;
    }

    std::shared_ptr<SF2File> sf2(coll->CreateSF2File());
    if (!sf2) {
        L_ERROR("No soundfont could be generated for '{}'. Playback aborted", coll->GetName());
        return;
    }

    auto sf2_data = SF2Wrapper::SetSF2(sf2.get());
    fluid_synth_sfload(m_synth, sf2_data, 0);

    std::shared_ptr<MidiFile> midi(seq->ConvertToMidi());
    std::vector<uint8_t> midi_buf;
    midi->WriteMidiToBuffer(midi_buf);

    makePlayer();

    if (fluid_player_add_mem(m_active_player, midi_buf.data(), midi_buf.size()) == FLUID_OK) {
        active_coll = coll;
    }
}
