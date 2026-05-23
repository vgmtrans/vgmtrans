#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS1Seq.h"
#include "CPS2Format.h"

enum CPS1FormatVer: uint8_t;

class CPS1TrackV2
    : public SeqTrack {
public:
  CPS1TrackV2(VGMSeq *parentSeq, CPSSynth channelSynth, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  void addInitialMidiEvents(int trackNum) override;
  bool readEvent() override;

private:
  CPS1FormatVer version() const { return (static_cast<CPS1Seq*>(this->parentSeq))->formatVersion(); }
  void calculateAndAddPortamentoTimeNoItem(int8_t noteDistance);

  CPSSynth channelSynth;
  uint8_t noteDuration;
  int8_t key;
  int8_t progNum = -1;
  bool bPrevNoteTie;
  uint8_t prevTieNote;
  uint8_t curDeltaTable;
  uint8_t noteState;
  uint8_t loop[4];
  uint32_t loopOffset[4];
  int16_t portamentoCentsPerSec;
  uint16_t prevPortamentoDuration;
};