/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "SoftCreatSnesFormat.h"

enum SoftCreatSnesSeqEventType {
    // start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get
    // confused with a legit event
    EVENT_UNKNOWN0 = 1,
    EVENT_UNKNOWN1,
    EVENT_UNKNOWN2,
    EVENT_UNKNOWN3,
    EVENT_UNKNOWN4,
};

class SoftCreatSnesSeq : public VGMSeq {
   public:
    SoftCreatSnesSeq(RawFile *file, SoftCreatSnesVersion ver, uint32_t seqdata_offset,
                     uint8_t headerAlignSize, std::wstring newName = L"SoftCreat SNES Seq");
    virtual ~SoftCreatSnesSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    virtual void ResetVars(void);

    SoftCreatSnesVersion version;
    std::map<uint8_t, SoftCreatSnesSeqEventType> EventMap;

   private:
    void LoadEventMap(void);

    uint8_t headerAlignSize;
};

class SoftCreatSnesTrack : public SeqTrack {
   public:
    SoftCreatSnesTrack(SoftCreatSnesSeq *parentFile, long offset = 0, long length = 0);
    virtual void ResetVars(void);
    virtual bool ReadEvent(void);
};
