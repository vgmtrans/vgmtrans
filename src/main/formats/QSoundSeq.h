/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "QSoundFormat.h"

enum QSoundVer : uint8_t;

class QSoundSeq : public VGMSeq {
   public:
    QSoundSeq(RawFile *file, uint32_t offset, QSoundVer fmt_version, std::string &name);
    virtual ~QSoundSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    virtual bool PostLoad(void);

   public:
    QSoundVer fmt_version;
};

class QSoundTrack : public SeqTrack {
   public:
    QSoundTrack(QSoundSeq *parentSeq, long offset = 0, long length = 0);
    virtual void ResetVars();
    virtual bool ReadEvent(void);

   private:
    QSoundVer GetVersion() { return ((QSoundSeq *)this->parentSeq)->fmt_version; }

    bool bPrevNoteTie;
    uint8_t prevTieNote;
    uint8_t origTieNote;
    uint8_t curDeltaTable;
    uint8_t noteState;
    uint8_t bank;
    uint8_t loop[4];
    uint32_t loopOffset[4];  // used for detecting infinite loops
};