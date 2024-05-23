#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "OrgFormat.h"
#include "OrgScanner.h"

class OrgSeq:
    public VGMSeq {
 public:
  OrgSeq(RawFile *file, uint32_t offset);
 public:
  virtual ~OrgSeq(void);

  virtual bool GetHeaderInfo(void);

 public:
  uint16_t waitTime;        //I believe this is the millis per tick
  uint8_t beatsPerMeasure;
};


class OrgTrack
    : public SeqTrack {
 public:
  OrgTrack(OrgSeq *parentFile, uint32_t offset, uint32_t length, uint8_t realTrk);

  virtual bool LoadTrack(uint32_t trackNum, uint32_t stopOffset, long stopDelta);
  virtual bool ReadEvent(void);

 public:
  uint8_t prevPan;

  uint16_t curNote;
  uint8_t realTrkNum;
  uint16_t freq;
  uint8_t waveNum;
  uint16_t numNotes;
};