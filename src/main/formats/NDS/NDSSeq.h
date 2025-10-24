#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "NDSFormat.h"

class NDSSeq:
    public VGMSeq {
 public:
  NDSSeq(RawFile *file, uint32_t offset, uint32_t length = 0, std::string theName = "NDSSeq");

  virtual bool parseHeader();
  virtual bool parseTrackPointers();

};


class NDSTrack
    : public SeqTrack {
 public:
  NDSTrack(NDSSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  void resetVars();
  virtual bool readEvent();

  uint32_t dur;
  bool noteWithDelta;
};