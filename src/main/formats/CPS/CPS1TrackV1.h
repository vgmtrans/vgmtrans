#pragma once

#include "base/Types.h"
#include "CPS1Seq.h"
#include "CPS2Format.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

class CPS1TrackV1
    : public SeqTrack {
public:
  CPS1TrackV1(CPS1Seq *parentSeq, CPSSynth channelSynth, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
  CPSSynth channelSynth;
  u8 noteDuration = 0;
  s8 key;
  s8 progNum = -1;
  s8 instrTranspose = 0;
  s8 cps0transpose = 0;
  bool restFlag = false;
  bool extendDeltaFlag = false;
  bool tieNoteFlag = false;
  u8 shortenDeltaCounter = 0;
  u8 tieNoteCounter = 0;

  bool bPrevNoteTie = false;
  u8 prevTieNote = 0;
  u8 curDeltaTable;
  u8 noteState;
  u8 bank;
  u8 loop[3];
  u32 loopOffset[3];
  s16 portamentoCentsPerSec;
  u16 prevPortamentoDuration;
};