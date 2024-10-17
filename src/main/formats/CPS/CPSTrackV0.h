#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS0Seq.h"
#include "CPS2Format.h"

class CPSTrackV0
    : public SeqTrack {
public:
  CPSTrackV0(CPS0Seq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
  CPSSynth channelSynth;
  u8 noteDuration;
  s8 key;
  s8 progNum = -1;
  s8 instrTranspose = 0;
  s8 cps0transpose = 0;
  bool restFlag = false;
  bool dottedNoteFlag = false;

  bool bPrevNoteTie;
  u8 prevTieNote;
  u8 curDeltaTable;
  u8 noteState;
  u8 bank;
  u8 loop[3];
  u32 loopOffset[3];
  s16 portamentoCentsPerSec;
  u16 prevPortamentoDuration;
};