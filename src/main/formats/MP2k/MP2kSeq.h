/*
 * VGMTrans (c) 2002-2019
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
  MP2kSeq(RawFile *file, uint32_t offset, std::string name = "MP2kSeq");
  ~MP2kSeq() = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
};

class MP2kTrack final : public SeqTrack {
public:
  MP2kTrack(MP2kSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  State readEvent() override;

private:
  uint8_t state = 0;
  uint32_t curDuration = 0;
  uint8_t current_vel = 0;

  std::vector<uint32_t> loopEndPositions;
  void handleStatusCommand(u32 offset, u8 status);
  void handleSpecialCommand(u32 offset, u8 status);
};

class MP2kEvent : public SeqEvent {
public:
  MP2kEvent(MP2kTrack *pTrack, uint8_t stateType);

private:
  // Keep record of the state, because otherwise, all 0-0x7F events are ambiguous
  uint8_t eventState;
};
