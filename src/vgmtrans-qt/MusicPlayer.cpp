/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MusicPlayer.h"

#include <QTemporaryDir>
#include <VGMSeq.h>

MusicPlayer::MusicPlayer() {
    makeSettings();
    makeSynth();
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

MusicPlayer &MusicPlayer::Instance() {
    static MusicPlayer gui_player;
    return gui_player;
}

void MusicPlayer::makeSettings() {
    /* Create settings if needed */
    if (!settings) {
        settings = new_fluid_settings();
/* Default to Pulseaudio on Linux */
#ifdef __linux__
        fluid_settings_setstr(settings, "audio.driver", "pulseaudio");
#endif
    }

#if FLUIDSYNTH_VERSION_MAJOR >= 2
    fluid_settings_setint(settings, "synth.reverb.active", 1);
    fluid_settings_setint(settings, "synth.chorus.active", 0);
#else
    fluid_settings_setstr(settings, "synth.reverb.active", "yes");
    fluid_settings_setstr(settings, "synth.chorus.active", "no");
#endif
    fluid_settings_setstr(settings, "synth.midi-bank-select", "mma");
    fluid_settings_setint(settings, "synth.midi-channels", 48);
    fluid_settings_setstr(settings, "player.timing-source", "system");
}

void MusicPlayer::makeSynth() {
    if (synth) {
        Stop();
        delete_fluid_synth(synth);
    }

    synth = new_fluid_synth(settings);

/* FluidSynth < 2 doesn't support reading SF2 from RAM */
#if FLUIDSYNTH_VERSION_MAJOR >= 2
    fluid_sfloader_t *loader = new_fluid_defsfloader(settings);

    fluid_sfloader_set_callbacks(loader, &SF2Wrapper::sf_open, &SF2Wrapper::sf_read,
                                 &SF2Wrapper::sf_seek, &SF2Wrapper::sf_tell, &SF2Wrapper::sf_close);

    fluid_synth_add_sfloader(synth, loader);
#endif
}

void MusicPlayer::updateSetting(const char *setting, int value) {
    fluid_settings_setint(settings, setting, value);
    makeSynth();
}

void MusicPlayer::updateSetting(const char *setting, const char *value) {
    fluid_settings_setstr(settings, setting, value);
    makeSynth();
}

bool MusicPlayer::checkSetting(const char *setting, const char *value) {
    return !!fluid_settings_str_equal(settings, setting, value);
}

void MusicPlayer::Toggle() {
    if (!player) {
        return;
    }

    if (SynthPlaying()) {
        fluid_player_stop(player);
    } else {
        fluid_player_play(player);
    }

    emit StatusChange(SynthPlaying());
}

void MusicPlayer::Stop() {
    if (player) {
        fluid_player_stop(player);
        delete_fluid_player(player);
        player = nullptr;
    }

    if (adriver) {
        delete_fluid_audio_driver(adriver);
        adriver = nullptr;
    }

    active_coll = nullptr;

    emit StatusChange(false);
}

bool MusicPlayer::SynthPlaying() {
    if (player && fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
        return true;
    }

    return false;
}

void MusicPlayer::LoadCollection(VGMColl *coll) {
    if (active_coll == coll) {
        return;
    }

    VGMSeq *seq = coll->GetSeq();
    if (!seq) {
        return;
    }

    std::shared_ptr<SF2File> sf2(coll->CreateSF2File());
    if (!sf2) {
        return;
    }

#if FLUIDSYNTH_VERSION_MAJOR >= 2
    SF2Wrapper::SetSF2(sf2);

    char abused_filename[64];
    const void *sf2_buf = sf2->SaveToMem();
    sprintf(abused_filename, "&%p", sf2_buf);
#else
    QTemporaryDir dir;
    std::string temp_sf2 = dir.path().toStdString() + "/temp";
    sf2->SaveSF2File(temp_sf2);
    const char *abused_filename = temp_sf2.c_str();
#endif

    sfont_id = fluid_synth_sfload(synth, abused_filename, 0);

    std::shared_ptr<MidiFile> midi(seq->ConvertToMidi());
    std::vector<uint8_t> midi_buf;
    midi->WriteMidiToBuffer(midi_buf);

    Stop();

    player = new_fluid_player(synth);
    adriver = new_fluid_audio_driver(settings, synth);
    if (fluid_player_add_mem(player, &midi_buf.at(0), midi_buf.size()) == FLUID_OK) {
        active_coll = coll;
    }
}
