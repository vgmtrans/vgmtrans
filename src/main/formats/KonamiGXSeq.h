/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiGXFormat.h"

class KonamiGXSeq : public VGMSeq {
   public:
    KonamiGXSeq(RawFile *file, uint32_t offset);
    virtual ~KonamiGXSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    // bool LoadTracks(void);

   protected:
};

class KonamiGXTrack : public SeqTrack {
   public:
    KonamiGXTrack(KonamiGXSeq *parentSeq, long offset = 0, long length = 0);

    virtual bool ReadEvent(void);

   private:
    bool bInJump;
    uint8_t prevDelta;
    uint8_t prevDur;
    uint32_t jump_return_offset;
};