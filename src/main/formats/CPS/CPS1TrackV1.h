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
  uint8_t noteDuration = 0;
  int8_t key;
  int8_t progNum = -1;
  int8_t instrTranspose = 0;
  int8_t cps0transpose = 0;
  bool restFlag = false;
  bool extendDeltaFlag = false;
  bool tieNoteFlag = false;
  uint8_t shortenDeltaCounter = 0;
  uint8_t tieNoteCounter = 0;

  bool bPrevNoteTie = false;
  uint8_t prevTieNote = 0;
  uint8_t curDeltaTable;
  uint8_t noteState;
  uint8_t bank;
  uint8_t loop[3];
  uint32_t loopOffset[3];
  int16_t portamentoCentsPerSec;
  uint16_t prevPortamentoDuration;
};