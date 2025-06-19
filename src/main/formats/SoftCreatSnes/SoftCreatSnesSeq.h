#pragma once
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
                   uint32_t seqdata_offset,
                   uint8_t headerAlignSize,
                   std::string newName = "SoftCreat SNES Seq");
  virtual ~SoftCreatSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  SoftCreatSnesVersion version;
  std::map<uint8_t, SoftCreatSnesSeqEventType> EventMap;

 private:
  void loadEventMap();

  uint8_t headerAlignSize;
};


class SoftCreatSnesTrack
    : public SeqTrack {
 public:
  SoftCreatSnesTrack(SoftCreatSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  virtual void resetVars();
  virtual State readEvent();
};
