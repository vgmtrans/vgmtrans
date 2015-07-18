//
// Created by Mike on 8/31/14.
//

#include <stdlib.h>
#include "MusicPlayer.h"

extern "C" {
#include "mem_sfloader.h"
#include "vgmtrans_fluid_midi.h"
}

int midi_event_callback(void* data, fluid_midi_event_t* event);

MusicPlayer::MusicPlayer()
{
    /* Create the settings. */
    this->settings = new_fluid_settings();

    fluid_sfloader_t* loader;

    /* Change the settings if necessary*/
    fluid_settings_setstr(this->settings, "synth.reverb.active", "yes");
    fluid_settings_setstr(this->settings, "synth.chorus.active", "no");
    fluid_settings_setstr(this->settings, "synth.midi-bank-select", "mma");
    fluid_settings_setint(this->settings, "synth.midi-channels", 48);
//    fluid_settings_setstr(this->settings, "player.timing-source", "system");

    /* Create the synthesizer. */
    this->synth = new_fluid_synth(this->settings);


    // allocate and add our custom memory sfont loader
    loader = new_memsfloader(settings);

    if (loader == NULL) {
        FLUID_LOG(FLUID_WARN, "Failed to create the default SoundFont loader");
    } else {
        fluid_synth_add_sfloader(synth, loader);
    }


//        fluid_synth_set_reverb(synth, 0.5, 0.8, FLUID_REVERB_DEFAULT_WIDTH, 0.5);
        fluid_synth_set_reverb(synth, 0.5, 0.8, FLUID_REVERB_DEFAULT_WIDTH, 0.3);
//    fluid_synth_set_reverb(this->synth, 0.7, 0.8, 0.9, 0.4);
    //fluid_synth_set_interp_method(synth, -1, FLUID_INTERP_NONE);

//    this->player = new_vgmtrans_fluid_player(this->synth);
//    fluid_player_set_playback_callback(this->player, &midi_event_callback, this->synth);
}

void MusicPlayer::Shutdown()
{
    delete_fluid_audio_driver(this->adriver);
    delete_fluid_player(this->player);
    delete_fluid_synth(this->synth);
    delete_fluid_settings(this->settings);
}

void MusicPlayer::LoadSF2(const void *data)
{
    if (this->sfont_id > 0 && fluid_synth_sfunload(this->synth, this->sfont_id, true) != 0) {
        printf("Error unloading soundfont");
    }

    // Our memory sfont loader will treat the filename param as a pointer to the sf2 in memory.
    // No other choice short of changing the fluidsynth code.
    this->sfont_id = fluid_synth_sfload(this->synth, (const char *)data, 0);
}

int midi_event_callback(void* data, fluid_midi_event_t* event)
{

	int track_num = (vgmtrans_fluid_midi_event_get_track((vgmtrans_fluid_midi_event_t*)event))->num;
    int chan = fluid_midi_event_get_channel(event);
    int new_chan = ((track_num / 15) * 16) + chan;
    fluid_midi_event_set_channel(event, new_chan);

	return fluid_synth_handle_midi_event(data, event);
}

void MusicPlayer::StopMidi() {
    
    if (this->player) {
        fluid_player_stop(this->player);
        delete_fluid_player(this->player);
    }
    if (this->adriver) {
        delete_fluid_audio_driver(this->adriver);
    }
}

void MusicPlayer::PlayMidi(const void* data, size_t len)
{
    this->player = new_fluid_player(this->synth);
    this->adriver = new_fluid_audio_driver(this->settings, this->synth);

    if (FLUID_OK == fluid_player_add_mem(this->player, data, len)) {
		fluid_player_set_playback_callback(this->player, &midi_event_callback, this->synth);
        vgmtrans_fluid_player_play(this->player);
	}
}
