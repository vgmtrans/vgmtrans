/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "automation/SeqMidiAutomation.h"

class MP2kSeq final : public VGMSeq {
public:
  MP2kSeq(RawFile *file, u32 offset, std::string name = "MP2kSeq");
  ~MP2kSeq() = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
};

class MP2kTrack final : public SeqTrack {
public:
  MP2kTrack(MP2kSeq *parentSeq, u32 offset = 0, u32 length = 0);

  bool readEvent() override;

protected:
  void resetVars() override;
  void onTickBegin() override;

private:
  enum class State : u8 {
    Note = 0,
    Tie = 1,
    TieEnd = 2,
    Vol = 3,
    Pan = 4,
    PitchBend = 5,
    Modulation = 6,
  };

  State state = State::Note;
  u32 curDuration = 0;
  u8 current_vel = 0;

  void beginNoteLfo();
  void updateLfoFade();
  void setLfoSpeed(u8 speed);
  void setLfoDelay(u8 delay);
  void setModulationDepth(u8 depth);
  void setModulationType(u8 type);
  void applyLfoDepth(bool force);
  void clearLfoOutputs();
  bool lfoOutputsEnabled() const;

  std::vector<u32> loopEndPositions;
  void handleStatusCommand(u32 offset, u8 status);
  void handleSpecialCommand(u32 offset, u8 status);

  u8 modType = 0;
  SeqSynthLfoAutomation vibratoLfo;
  SeqSynthLfoAutomation tremoloLfo;
};
