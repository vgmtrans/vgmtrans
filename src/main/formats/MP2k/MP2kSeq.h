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
  enum class State : uint8_t {
    Note = 0,
    Tie = 1,
    TieEnd = 2,
    Vol = 3,
    Pan = 4,
    PitchBend = 5,
    Modulation = 6,
  };

  State state = State::Note;
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
