#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPSSeq.h"
#include "CPS2Format.h"

enum CPSFormatVer: uint8_t;

class CPSTrackV1
    : public SeqTrack {
public:
  CPSTrackV1(CPSSeq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
  CPSFormatVer version() const { return (static_cast<CPSSeq*>(this->parentSeq))->fmt_version; }
  void calculateAndAddPortamentoTimeNoItem(int8_t noteDistance);

  CPSSynth channelSynth;
  u8 noteDuration;
  s8 key;
  bool bPrevNoteTie;
  u8 prevTieNote;
  u8 curDeltaTable;
  u8 noteState;
  u8 bank;
  u8 loop[4];
  u32 loopOffset[4];    //used for detecting infinite loops
  s16 portamentoCentsPerSec;
  u16 prevPortamentoDuration;
};