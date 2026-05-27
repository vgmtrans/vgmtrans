#pragma once

#include "base/types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "OrgFormat.h"
#include "OrgScanner.h"

class OrgSeq:
    public VGMSeq {
 public:
  OrgSeq(RawFile *file, u32 offset);
 public:
  virtual ~OrgSeq();

  virtual bool parseHeader();

 public:
  u16 waitTime;        //I believe this is the millis per tick
  u8 beatsPerMeasure;
};


class OrgTrack
    : public SeqTrack {
 public:
  OrgTrack(OrgSeq *parentFile, u32 offset, u32 length, u8 realTrk);

  virtual bool loadTrack(u32 trackNum, u32 stopOffset, long stopDelta);
  virtual bool readEvent();

 public:
  u8 prevPan;

  u16 curNote;
  u8 realTrkNum;
  u16 freq;
  u8 waveNum;
  u16 numNotes;
};