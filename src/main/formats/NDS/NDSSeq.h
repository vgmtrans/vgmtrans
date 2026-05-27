#pragma once

#include "base/types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "NDSFormat.h"

class NDSSeq:
    public VGMSeq {
 public:
  NDSSeq(RawFile *file, u32 offset, u32 length = 0, std::string theName = "NDSSeq");

  virtual bool parseHeader();
  virtual bool parseTrackPointers();

};


class NDSTrack
    : public SeqTrack {
 public:
  NDSTrack(NDSSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars();
  virtual bool readEvent();

  u32 dur;
  bool noteWithDelta;
};