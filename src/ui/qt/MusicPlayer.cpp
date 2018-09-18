/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MusicPlayer.h"
#include "VGMSeq.h"
#include "SF2File.h"
#include <QTemporaryDir>

MusicPlayer::MusicPlayer() {
  /* Create the settings. */
  settings = new_fluid_settings();

// Lines commented out are for temporary
// fluidsynth 1.0 compatibility
//  fluid_sfloader_t* loader;

  fluid_settings_setstr(settings, "synth.reverb.active", "yes");
  fluid_settings_setstr(settings, "synth.chorus.active", "no");
  fluid_settings_setstr(settings, "synth.midi-bank-select", "mma");
  fluid_settings_setint(settings, "synth.midi-channels", 48);
  fluid_settings_setstr(settings, "player.timing-source", "system");

/* Default to Pulseaudio on Linux */
#ifdef __linux__
  fluid_settings_setstr(settings, "audio.driver", "pulseaudio");
#endif

  synth = new_fluid_synth(settings);
//  loader = new_fluid_defsfloader(settings);
//  fluid_synth_add_sfloader(synth, loader);
}

MusicPlayer& MusicPlayer::Instance() {
  static MusicPlayer gui_player;
  return gui_player;
}

void MusicPlayer::Toggle() {
  if(player && !SynthPlaying()) {
    fluid_player_play(player);
  } else {
    fluid_player_stop(player);
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
  if(player && fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
    return true;
  }

  return false;
}

void MusicPlayer::LoadCollection(VGMColl *coll) {

  if(active_coll == coll) {
    return; // Already loaded
  }

  VGMSeq *seq = coll->GetSeq();
  SF2File *sf2 = coll->CreateSF2File();
  MidiFile *midi = seq->ConvertToMidi();

  std::vector<uint8_t> midi_buf;
  midi->WriteMidiToBuffer(midi_buf);

  //FIXME: Load SF2 from memory (requires fluidsynth 2.0)
  QTemporaryDir dir;
  std::wstring temp_sf2 = dir.path().toStdWString() + L"/" + L"temp";
  sf2->SaveSF2File(temp_sf2);

  char temp2[temp_sf2.length()+1];
  std::wcstombs(temp2, temp_sf2.c_str(), sizeof temp2);
  sfont_id = fluid_synth_sfload(synth, temp2, 0);

  Stop();

  player = new_fluid_player(synth);
  adriver = new_fluid_audio_driver(settings, synth);
  if(fluid_player_add_mem(player, &midi_buf.at(0), midi_buf.size()) == FLUID_OK) {
    active_coll = coll;
  }

  return;

}
