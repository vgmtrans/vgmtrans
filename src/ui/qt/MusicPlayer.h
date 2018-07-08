/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include "VGMColl.h"

extern "C" {
  #include <fluidsynth.h>
}

class MusicPlayer : public QObject {
  Q_OBJECT

public:
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;
  MusicPlayer(MusicPlayer&&) = delete;
  MusicPlayer& operator=(MusicPlayer&&) = delete;

  static MusicPlayer& Instance();

  bool SynthPlaying();
  void LoadCollection(VGMColl* coll);
  void Toggle();
  void Stop();

signals:
  void StatusChange(bool playing);

private:
  fluid_settings_t* settings = nullptr;
  fluid_synth_t* synth = nullptr;
  fluid_audio_driver_t* adriver = nullptr;
  fluid_player_t* player = nullptr;
  int sfont_id;
  VGMColl* active_coll = nullptr;

  MusicPlayer();

};

