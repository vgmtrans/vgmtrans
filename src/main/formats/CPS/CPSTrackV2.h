#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPSSeq.h"

enum CPSFormatVer: uint8_t;


enum CPSv2SeqEventType {
  EVENT_NOP = 0xC0,
  C1_TEMPO,
  C2_SETBANK,
  C3_PITCHBEND,
  C4_PROGCHANGE,
  C5_VIBRATO,
  C6_VOLUME,
  EVENT_C7,
  EVENT_C8,
  C9_PORTAMENTO,
  EVENT_CA,
  EVENT_CB,
  EVENT_CC,
  EVENT_CD,
  EVENT_CE,
  EVENT_CF,
  D0_LOOP_1_START,
  D1_LOOP_2_START,
  D2_LOOP_3_START,
  D3_LOOP_4_START,
  D4_LOOP_1,
  D5_LOOP_2,
  D6_LOOP_3,
  D7_LOOP_4,
  D8_LOOP_1_BREAK,
  D9_LOOP_2_BREAK,
  DA_LOOP_3_BREAK,
  DB_LOOP_4_BREAK,
  EVENT_DC,
  DD_TRANSPOSE,
  EVENT_DE,
  EVENT_DF,
  E0_RESET_LFO,
  E1_LFO_RATE,
  E2_TREMELO,
  EVENT_E3,
  EVENT_E4,
  EVENT_E5,
  EVENT_E6,
  EVENT_E7,
  EVENT_E8,
  FF_END = 0XFF,
};


// Class for 2.xx CPS sequences. This iteration of the format is found in CPS3 games, ZN1-based games, and oddly,
// a single CPS2 game: Super Puzzle Fighter 2 Turbo.
class CPSTrackV2
    : public SeqTrack {
public:
  CPSTrackV2(CPSSeq *parentSeq, long offset = 0, long length = 0);
  void ResetVars() override;
  bool ReadEvent() override;

private:
  CPSFormatVer GetVersion() const { return static_cast<CPSSeq*>(this->parentSeq)->fmt_version; }
  uint32_t ReadVarLength();

  bool bPrevNoteTie;
  int8_t loopCounter[4];
  uint32_t loopOffset[4];    //used for detecting infinite loops
};
