/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "NDSFormat.h"

class NDSSeq : public VGMSeq {
   public:
    NDSSeq(RawFile *file, uint32_t offset, uint32_t length = 0, std::string theName = "NDSSeq");

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
};

class NDSTrack : public SeqTrack {
   public:
    NDSTrack(NDSSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
    void ResetVars();
    virtual bool ReadEvent(void);

    uint8_t jumpCount;
    uint32_t loopReturnOffset;
    bool hasLoopReturnOffset;
    bool noteWithDelta;
};