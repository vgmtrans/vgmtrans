#pragma once

#include "base/Types.h"
#include "ChunSnesFormat.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>

#define CHUNSNES_SUBLEVEL_MAX   3

enum ChunSnesSeqEventType {
  EVENT_UNKNOWN0 =
  1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOP,
  EVENT_NOTE,
  EVENT_DURATION_FROM_TABLE,
  EVENT_LOOP_BREAK_ALT,
  EVENT_LOOP_AGAIN_ALT,
  EVENT_ADSR_RELEASE_SR,
  EVENT_ADSR_AND_RELEASE_SR,
  EVENT_SURROUND,
  EVENT_CONDITIONAL_JUMP,
  EVENT_INC_COUNTER,
  EVENT_PITCH_ENVELOPE,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_MASTER_VOLUME_FADE,
  EVENT_EXPRESSION_FADE,
  EVENT_PAN_FADE,
  EVENT_TUNING,
  EVENT_FULL_VOLUME_FADE,
  EVENT_GOTO,
  EVENT_TEMPO,
  EVENT_DURATION_RATE,
  EVENT_VOLUME,
  EVENT_PAN,
  EVENT_ADSR,
  EVENT_PROGCHANGE,
  EVENT_SYNC_NOTE_LEN_ON,
  EVENT_SYNC_NOTE_LEN_OFF,
  EVENT_LOOP_AGAIN,
  EVENT_LOOP_UNTIL,
  EVENT_EXPRESSION,
  EVENT_CALL,
  EVENT_RET,
  EVENT_TRANSPOSE,
  EVENT_PITCH_SLIDE,
  EVENT_ECHO_ON,
  EVENT_ECHO_OFF,
  EVENT_LOAD_PRESET,
  EVENT_END,
};

enum ChunSnesSeqPresetType {
  PRESET_CONDITION =
  1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
};

class ChunSnesSeq
    : public VGMSeq {
 public:
  ChunSnesSeq(RawFile *file,
              ChunSnesVersion ver,
              ChunSnesMinorVersion minorVer,
              u32 seqdataOffset,
              std::string newName = "Chun SNES Seq");
  ~ChunSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  static double getTempoInBPM(u8 tempo);

  ChunSnesVersion version;
  ChunSnesMinorVersion minorVersion;
  std::map<u8, ChunSnesSeqEventType> EventMap;
  std::map<u8, ChunSnesSeqPresetType> PresetMap;

  u8 initialTempo;
  u8 conditionVar;

 private:
  void loadEventMap();
};


class ChunSnesTrack
    : public SeqTrack {
 public:
  ChunSnesTrack(ChunSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

  void syncNoteLengthWithPriorTrack();
  static u8 multiply8bit(u8 multiplicand, u8 multiplier);
  static void getVolumeBalance(s8 pan, double &volumeLeft, double &volumeRight);
  static s8 calcPanValue(s8 pan, double &volumeScale);
  static double calcTuningValue(s8 tuning);

  u8 index;
  s8 prevNoteKey;
  bool prevNoteSlurred;

  u8 noteLength;
  u8 noteDurationRate;
  bool syncNoteLen;

  u8 loopCount;
  u8 loopCountAlt;
  u8 subNestLevel;
  u16 subReturnAddr[CHUNSNES_SUBLEVEL_MAX];
};
