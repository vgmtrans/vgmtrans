#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS2Seq.h"
#include "CPS2Format.h"

enum CPS2FormatVer: uint8_t;

class CPS2TrackV1
    : public SeqTrack {
public:
  CPS2TrackV1(VGMSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
  CPS2FormatVer version() const { return (static_cast<CPS2Seq*>(this->parentSeq))->fmt_version; }
  void calculateAndAddPortamentoTimeNoItem(int8_t noteDistance);

  u8 noteDuration;
  s8 key;
  s8 progNum = -1;
  bool bPrevNoteTie;
  u8 prevTieNote;
  u8 curDeltaTable;
  u8 noteState;
  u8 bank;
  u8 loop[4];
  u32 loopOffset[4];
  s16 portamentoCentsPerSec;
  u16 prevPortamentoDuration;
};