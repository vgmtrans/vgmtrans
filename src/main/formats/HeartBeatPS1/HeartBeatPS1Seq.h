/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeqNoTrks.h"

class HeartBeatPS1Seq:
    public VGMSeqNoTrks {
 public:
  HeartBeatPS1Seq(RawFile *file, uint32_t offset, uint32_t length = 0, const std::string &name = "HeartBeatPS1Seq");
  virtual ~HeartBeatPS1Seq(void);

  virtual bool GetHeaderInfo(void);
  virtual void ResetVars();
  virtual bool ReadEvent(void);

 protected:
  uint32_t seqHeaderOffset;
  uint8_t runningStatus;
};
