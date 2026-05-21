/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "automation/SeqMidiAutomation.h"
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

  bool readEvent() override;

protected:
  void resetVars() override;
  void onTickBegin() override;

private:
  uint8_t state = 0;
  uint32_t curDuration = 0;
  uint8_t current_vel = 0;

  void beginNoteLfo();
  void updateLfoFade();
  void setLfoSpeed(uint8_t speed);
  void setLfoDelay(uint8_t delay);
  void setModulationDepth(uint8_t depth);
  void setModulationType(uint8_t type);
  void applyLfoDepth(bool force);
  void clearLfoOutputs();
  bool lfoOutputsEnabled() const;

  std::vector<uint32_t> loopEndPositions;
  void handleStatusCommand(u32 offset, u8 status);
  void handleSpecialCommand(u32 offset, u8 status);

  uint8_t modType = 0;
  SeqSynthLfoAutomation vibratoLfo;
  SeqSynthLfoAutomation tremoloLfo;
};

class MP2kEvent : public SeqEvent {
public:
  MP2kEvent(MP2kTrack *pTrack, uint8_t stateType);

private:
  // Keep record of the state, because otherwise, all 0-0x7F events are ambiguous
  uint8_t eventState;
};
