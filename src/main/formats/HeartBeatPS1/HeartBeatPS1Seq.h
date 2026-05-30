/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMSeqNoTrks.h"

#include <string>

class HeartBeatPS1Seq:
    public VGMSeqNoTrks {
 public:
  HeartBeatPS1Seq(RawFile *file, u32 offset, u32 length = 0, const std::string &name = "HeartBeatPS1Seq");
  ~HeartBeatPS1Seq() override;

  bool parseHeader() override;
  void resetVars() override;
  bool readEvent() override;

 private:
  u32 seqHeaderOffset;
  u8 runningStatus;
};
