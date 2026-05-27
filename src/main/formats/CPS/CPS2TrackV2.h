#pragma once

#include "base/Types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS2Seq.h"

enum CPS2FormatVer: u8;


enum CPSv2SeqEventType {
  EVENT_NOP = 0xC0,
  C1_TEMPO,
  C2_SETBANK,
  C3_PITCHBEND,
  C4_PROGCHANGE,
  C5_VIBRATO,
  C6_VOLUME,
  C7_PAN,
  C8_EXPRESSION,
  C9_PORTAMENTO,
  EVENT_CA,
  EVENT_CB,
  EVENT_CC,
  EVENT_CD,
  CE_GOTO,
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
  E7_FINE_TUNE,
  E8_META_EVENT,
  FF_END = 0XFF,
};


// Class for 2.xx CPS sequences. This iteration of the format is found in CPS3 games, ZN1-based games, and oddly,
// a single CPS2 game: Super Puzzle Fighter 2 Turbo.
class CPS2TrackV2
    : public SeqTrack {
public:
  CPS2TrackV2(CPS2Seq *parentSeq, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

private:
  CPS2FormatVer version() const { return static_cast<CPS2Seq*>(this->parentSeq)->fmt_version; }
  u32 readVarLength();

  u8 m_volume{0};
  u8 m_expression{0x40};
  s8 loopCounter[4];
  u32 loopOffset[4];    //used for detecting infinite loops
};
