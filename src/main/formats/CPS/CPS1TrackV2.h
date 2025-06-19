#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS1Seq.h"
#include "CPS2Format.h"

enum CPS1FormatVer: u8;

class CPS1TrackV2
    : public SeqTrack {
public:
  CPS1TrackV2(VGMSeq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  State readEvent() override;

private:
  CPS1FormatVer version() const { return (static_cast<CPS1Seq*>(this->parentSeq))->formatVersion(); }
  void calculateAndAddPortamentoTimeNoItem(int8_t noteDistance);

  CPSSynth channelSynth;
  u8 noteDuration;
  s8 key;
  s8 progNum = -1;
  bool bPrevNoteTie;
  u8 prevTieNote;
  u8 curDeltaTable;
  u8 noteState;
  u8 loop[4];
  u32 loopOffset[4];
  s16 portamentoCentsPerSec;
  u16 prevPortamentoDuration;
};