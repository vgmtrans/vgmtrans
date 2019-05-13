/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "VGMSeqNoTrks.h"
#include "SegSatFormat.h"
#include "SegSatScanner.h"

class SegSatSeq:
    public VGMSeqNoTrks {
 public:
  SegSatSeq(RawFile *file, uint32_t offset);
  virtual ~SegSatSeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool ReadEvent(void);

 public:
  uint8_t headerFlag;
  int remainingEventsInLoop;
  uint32_t loopEndPos;
};