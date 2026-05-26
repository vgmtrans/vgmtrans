#pragma once

#include "util/types.h"
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
  NeverlandSnesSeq(RawFile *file, NeverlandSnesVersion ver, u32 seqdataOffset);
  virtual ~NeverlandSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  NeverlandSnesVersion version;
  std::map<u8, NeverlandSnesSeqEventType> EventMap;

  u16 convertToApuAddress(u16 off);
  u16 getShortAddress(u32 offset);

 private:
  void loadEventMap();
};


class NeverlandSnesTrack
    : public SeqTrack {
 public:
  NeverlandSnesTrack(NeverlandSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();

  u16 convertToApuAddress(u16 offset);
  u16 getShortAddress(u32 offset);
};
