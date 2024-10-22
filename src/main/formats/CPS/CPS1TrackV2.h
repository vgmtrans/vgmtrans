#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
<<<<<<<< HEAD:src/main/formats/CPS/CPS1TrackV2.h
#include "CPS1Seq.h"
========
#include "CPS2Seq.h"
>>>>>>>> 14a4075d (reorganize CPS classes):src/main/formats/CPS/CPS2TrackV1.h
#include "CPS2Format.h"

enum CPS1FormatVer: u8;

<<<<<<<< HEAD:src/main/formats/CPS/CPS1TrackV2.h
class CPS1TrackV2
    : public SeqTrack {
public:
  CPS1TrackV2(VGMSeq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
========
class CPS2TrackV1
    : public SeqTrack {
public:
  CPS2TrackV1(VGMSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);
>>>>>>>> 14a4075d (reorganize CPS classes):src/main/formats/CPS/CPS2TrackV1.h
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
<<<<<<<< HEAD:src/main/formats/CPS/CPS1TrackV2.h
  CPS1FormatVer version() const { return (static_cast<CPS1Seq*>(this->parentSeq))->formatVersion(); }
========
  CPSFormatVer version() const { return (static_cast<CPS2Seq*>(this->parentSeq))->fmt_version; }
>>>>>>>> 14a4075d (reorganize CPS classes):src/main/formats/CPS/CPS2TrackV1.h
  void calculateAndAddPortamentoTimeNoItem(int8_t noteDistance);

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