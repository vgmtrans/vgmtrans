#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiGXFormat.h"

class KonamiGXSeq:
    public VGMSeq {
 public:
  KonamiGXSeq(RawFile *file, uint32_t offset);
  virtual ~KonamiGXSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  //bool LoadTracks(void);

 protected:

};


class KonamiGXTrack
    : public SeqTrack {
 public:
  KonamiGXTrack(KonamiGXSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  virtual bool readEvent(void);

 private:
  bool bInJump;
  uint8_t prevDelta;
  uint8_t prevDur;
  uint32_t jump_return_offset;
};