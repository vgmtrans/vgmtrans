#pragma once
#include "VGMSeqNoTrks.h"

class SegSatSeq:
    public VGMSeqNoTrks {
 public:
  SegSatSeq(RawFile *file, uint32_t offset, std::string name);
  ~SegSatSeq() override = default;

  void resetVars() override;
  bool parseHeader() override;
  bool readEvent() override;

 public:
  u32 normalTrackOffset;
  int remainingEventsInLoop = -1;
  u32 loopEndPos = -1;
  u32 foreverLoopStart = -1;
  u32 durationAccumulator = 0;
};