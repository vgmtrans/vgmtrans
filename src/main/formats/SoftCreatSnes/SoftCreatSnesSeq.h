#pragma once

#include "util/types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "SoftCreatSnesFormat.h"

enum SoftCreatSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
};

class SoftCreatSnesSeq
    : public VGMSeq {
 public:
  SoftCreatSnesSeq(RawFile *file,
                   SoftCreatSnesVersion ver,
                   u32 seqdata_offset,
                   u8 headerAlignSize,
                   std::string newName = "SoftCreat SNES Seq");
  virtual ~SoftCreatSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  SoftCreatSnesVersion version;
  std::map<u8, SoftCreatSnesSeqEventType> EventMap;

 private:
  void loadEventMap();

  u8 headerAlignSize;
};


class SoftCreatSnesTrack
    : public SeqTrack {
 public:
  SoftCreatSnesTrack(SoftCreatSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();
};
