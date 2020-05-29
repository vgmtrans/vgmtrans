/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "KonamiPS1Format.h"

class KonamiPS1Seq : public VGMSeq {
   public:
    static constexpr uint32_t kHeaderSize = 16;
    static constexpr uint32_t kOffsetToFileSize = 4;
    static constexpr uint32_t kOffsetToTimebase = 8;
    static constexpr uint32_t kOffsetToTrackCount = 0x0c;
    static constexpr uint32_t kOffsetToTrackSizes = 0x10;

    KonamiPS1Seq(RawFile *file, uint32_t offset, const std::string &name = "KonamiPS1Seq");

    virtual ~KonamiPS1Seq() {}

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    virtual void ResetVars(void);

    static bool IsKDT1Seq(RawFile *file, uint32_t offset);

    static uint32_t GetKDT1FileSize(RawFile *file, uint32_t offset) {
        return kHeaderSize + file->GetWord(offset + kOffsetToFileSize);
    }
};

class KonamiPS1Track : public SeqTrack {
   public:
    KonamiPS1Track(KonamiPS1Seq *parentSeq, uint32_t offset, uint32_t length);

    virtual void ResetVars(void);
    virtual bool ReadEvent(void);

   private:
    bool skipDeltaTime;
};
