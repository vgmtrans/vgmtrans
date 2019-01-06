/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include <cstring>
#include "VGMColl.h"
#include "SF2File.h"

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

  ~MusicPlayer();

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

  explicit MusicPlayer();

};

/* Not the most elegant way, but it's better than
   messing with the build system */
#if FLUIDSYNTH_VERSION_MAJOR >= 2

/*
 * This class represents a static wrapper to get around
 * C pointer-to-function requirements and implements
 * IO routines to allow fluidsynth to read soundfonts
 * from memory.
 */

class SF2Wrapper {

public:
  static void SetSF2(SF2File& obj) {
    if(sf2_obj_) {
      delete sf2_obj_;
    }
    
    if(old_sf2_buf_) {
      delete static_cast<char*>(old_sf2_buf_);
      old_sf2_buf_ = nullptr;
    }

    sf2_obj_ = &obj;
    index_ = 0;
  }

  static void* sf_open(const char* filename) {
    void *sf2_buf;

    if(filename[0] != '&') {
        return nullptr;
    }

    sscanf(filename, "&%p", &sf2_buf);

    old_sf2_buf_ = sf2_buf;

    return sf2_buf;
  }

  static int sf_read(void *buf, int count, void *handle) {

    char *newhandle = (char *) handle;
    newhandle += index_;

    memcpy(buf, newhandle, count);

    index_ += count;

    return FLUID_OK;
  }

  static int sf_seek(void *handle, long offset, int origin) {
    switch(origin) {
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
    if(file == old_sf2_buf_ && old_sf2_buf_ != nullptr) {
      delete static_cast<char*>(old_sf2_buf_);
      old_sf2_buf_ = nullptr;
    }

    return FLUID_OK; 
  }

private:
  static SF2File* sf2_obj_;
  static void* old_sf2_buf_;
  static long index_;

};

#endif