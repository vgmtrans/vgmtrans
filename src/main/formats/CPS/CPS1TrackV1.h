#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS1Seq.h"
#include "CPS2Format.h"

class CPS1TrackV1
    : public SeqTrack {
public:
  CPS1TrackV1(CPS1Seq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
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