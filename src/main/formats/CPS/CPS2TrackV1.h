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

  uint8_t noteDuration;
  int8_t key;
  int8_t progNum = -1;
  bool bPrevNoteTie;
  uint8_t prevTieNote;
  uint8_t curDeltaTable;
  uint8_t noteState;
  uint8_t bank;
  uint8_t loop[4];
  uint32_t loopOffset[4];
  int16_t portamentoCentsPerSec;
  uint16_t prevPortamentoDuration;
};