//
// Created by Mike on 8/31/14.
//

#include "fluidsynth.h"

#ifndef __MusicPlayer_H_
#define __MusicPlayer_H_

class MusicPlayer {

private:
    // Private Constructor
    MusicPlayer();
    // Stop the compiler generating methods of copy the object
    MusicPlayer(MusicPlayer const& copy);            // Not Implemented
    MusicPlayer& operator=(MusicPlayer const& copy); // Not Implemented

public:
    static MusicPlayer& getInstance()
    {
        static MusicPlayer instance;
        return instance;
    }

    void Shutdown();
    void LoadSF2(const char *filename);
    void PlayMidi(const char *midiFile);
    void StopMidi();


private:
    fluid_settings_t* settings;
    fluid_synth_t* synth;
    fluid_audio_driver_t* adriver;
    fluid_player_t* player;
    int sfont_id;
};


#endif //__MusicPlayer_H_
