#pragma once
#include "VGMSeqNoTrks.h"
#include "SegSatFormat.h"
#include "SegSatScanner.h"

class SegSatSeq:
    public VGMSeqNoTrks {
 public:
  SegSatSeq(RawFile *file, uint32_t offset);
  virtual ~SegSatSeq();

  virtual bool GetHeaderInfo();
  virtual bool ReadEvent();

 public:
  uint8_t headerFlag;
  int remainingEventsInLoop;
  uint32_t loopEndPos;
};