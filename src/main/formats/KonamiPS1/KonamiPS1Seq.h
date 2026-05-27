#pragma once

#include "Types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "KonamiPS1Format.h"

class KonamiPS1Seq : public VGMSeq {
public:
    static constexpr u32 kHeaderSize = 16;
    static constexpr u32 kOffsetToFileSize = 4;
    static constexpr u32 kOffsetToTimebase = 8;
    static constexpr u32 kOffsetToTrackCount = 0x0c;
    static constexpr u32 kOffsetToTrackSizes = 0x10;

    KonamiPS1Seq(RawFile *file, u32 offset, const std::string &name = "KonamiPS1Seq");

    virtual ~KonamiPS1Seq() {
    }

    virtual bool parseHeader();
    virtual bool parseTrackPointers();
    virtual void resetVars();

    static bool isKDT1Seq(RawFile *file, u32 offset);

    static u32 getKDT1FileSize(RawFile *file, u32 offset) {
        return kHeaderSize + file->readWord(offset + kOffsetToFileSize);
    }
};

class KonamiPS1Track : public SeqTrack {
public:
    KonamiPS1Track(KonamiPS1Seq *parentSeq, u32 offset, u32 length);

    virtual void resetVars();
    virtual bool readEvent();

private:
    bool skipDeltaTime;
};
