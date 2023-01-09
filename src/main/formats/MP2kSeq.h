/*
 * VGMTrans (c) 2002-2023
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "MP2kFormat.h"

class MP2kSeq final : public VGMSeq {
   public:
    MP2kSeq(RawFile *file, uint32_t offset, std::wstring name = L"MP2kSeq");
    ~MP2kSeq() = default;

    bool GetHeaderInfo(void) override;
    bool GetTrackPointers(void) override;
};

class MP2kTrack final : public SeqTrack {
   public:
    MP2kTrack(MP2kSeq *parentSeq, long offset = 0, long length = 0);

    bool ReadEvent(void) override;

   private:
    uint8_t state = 0;
    uint32_t curDuration = 0;
    uint8_t current_vel = 0;

    std::vector<uint32_t> loopEndPositions;
    void handleStatusCommand(uint32_t offset, uint8_t status);
    void handleSpecialCommand(uint32_t offset, uint8_t status);
};

class MP2kEvent : public SeqEvent {
   public:
    MP2kEvent(MP2kTrack *pTrack, uint8_t stateType);

   private:
    // Keep record of the state, because otherwise, all 0-0x7F events are ambiguous
    uint8_t eventState;
};
