//
// Created by Mike on 8/31/14.
//

#include <fluidsynth.h>

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
    void LoadSF2(const void* sf2Data);
    void Play(const void *midiData, size_t len);
    void Pause();
    void Stop();


private:
    fluid_settings_t* settings = NULL;
    fluid_synth_t* synth = NULL;
    fluid_audio_driver_t* adriver = NULL;
    fluid_player_t* player = NULL;
    int sfont_id;
};


#endif //__MusicPlayer_H_
