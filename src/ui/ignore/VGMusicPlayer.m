//
//  VGMusicPlayer.m
//  VGMTrans
//
//  Created by Mike on 8/28/14.
//
//

#import "VGMusicPlayer.h"
#include "fluidsynth.h"

@implementation VGMusicPlayer

- (void) PlaySong
{
//    fluid_settings_t* settings;
//    fluid_synth_t* synth;
//    fluid_audio_driver_t* adriver;
//    int sfont_id;
//    int i, key;
//
//    /* Create the settings. */
//    settings = new_fluid_settings();
//
//    /* Change the settings if necessary*/
//
//    /* Create the synthesizer. */
//    synth = new_fluid_synth(settings);
//
//    /* Create the audio driver. The synthesizer starts playing as soon
//     as the driver is created. */
//    adriver = new_fluid_audio_driver(settings, synth);
//
//    /* Load a SoundFont*/
//    string filename = [[[NSBundle mainBundle] pathForResource:@"fft" ofType:@"sf2"] UTF8String];
//    sfont_id = fluid_synth_sfload(synth, filename.c_str(), 0);
//
//    /* Select bank 0 and preset 0 in the SoundFont we just loaded on
//     channel 0 */
//    fluid_synth_program_select(synth, 0, sfont_id, 0, 42);
//
//    /* Initialze the random number generator */
//    srand(getpid());
//
//    for (i = 0; i < 12; i++) {
//
//        /* Generate a random key */
//        key = 60 + (int) (12.0f * rand() / (float) RAND_MAX);
//
//        /* Play a note */
//        fluid_synth_noteon(synth, 0, key, 80);
//
//        /* Sleep for 1 second */
//        sleep(1);
//
//        /* Stop the note */
//        fluid_synth_noteoff(synth, 0, key);
//    }
//
//    /* Clean up */
//    delete_fluid_audio_driver(adriver);
//    delete_fluid_synth(synth);
//    delete_fluid_settings(settings);
}

- (void)PlaySong:(const void*)midi sf2:(const void*)sf2
{
//    int i;
//    fluid_settings_t* settings;
//    fluid_synth_t* synth;
//    fluid_player_t* player;
//    fluid_audio_driver_t* adriver;
//    settings = new_fluid_settings();
//    synth = new_fluid_synth(settings);
//    player = new_fluid_player(synth);
//    adriver = new_fluid_audio_driver(settings, synth);
//    /* process command line arguments */
//    for (i = 1; i < argc; i++) {
//        if (fluid_is_soundfont(argv[i])) {
//            fluid_synth_sfload(synth, argv[1], 1);
//        }
//        if (fluid_is_midifile(argv[i])) {
//            fluid_player_add(player, argv[i]);
//        }
//    }
//    /* play the midi files, if any */
//    fluid_player_play(player);
//    /* wait for playback termination */
//    fluid_player_join(player);
//    /* cleanup */
//    delete_fluid_audio_driver(adriver);
//    delete_fluid_player(player);
//    delete_fluid_synth(synth);
//    delete_fluid_settings(settings);
}

@end
