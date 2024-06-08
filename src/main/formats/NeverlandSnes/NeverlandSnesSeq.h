#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "NeverlandSnesFormat.h"

enum NeverlandSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
};

class NeverlandSnesSeq
    : public VGMSeq {
 public:
  NeverlandSnesSeq(RawFile *file, NeverlandSnesVersion ver, uint32_t seqdataOffset);
  virtual ~NeverlandSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  NeverlandSnesVersion version;
  std::map<uint8_t, NeverlandSnesSeqEventType> EventMap;

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);

 private:
  void loadEventMap();
};


class NeverlandSnesTrack
    : public SeqTrack {
 public:
  NeverlandSnesTrack(NeverlandSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  virtual void resetVars();
  virtual bool readEvent();

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
};
