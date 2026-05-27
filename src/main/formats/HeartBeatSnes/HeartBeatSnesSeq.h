#pragma once

#include "base/types.h"
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "HeartBeatSnesFormat.h"

enum HeartBeatSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_END,
  EVENT_NOTE_LENGTH,
  EVENT_NOTE,
  EVENT_TIE,
  EVENT_REST,
  EVENT_SLUR_ON,
  EVENT_SLUR_OFF,
  EVENT_PROGCHANGE,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_VIBRATO_ON,
  EVENT_VIBRATO_FADE,
  EVENT_VIBRATO_OFF,
  EVENT_MASTER_VOLUME,
  EVENT_MASTER_VOLUME_FADE,
  EVENT_TEMPO,
  EVENT_GLOBAL_TRANSPOSE,
  EVENT_TRANSPOSE,
  EVENT_TREMOLO_ON,
  EVENT_TREMOLO_OFF,
  EVENT_VOLUME,
  EVENT_VOLUME_FADE,
  EVENT_PITCH_ENVELOPE_TO,
  EVENT_PITCH_ENVELOPE_FROM,
  EVENT_PITCH_ENVELOPE_OFF,
  EVENT_TUNING,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_OFF,
  EVENT_ECHO_ON,
  EVENT_ECHO_FIR,
  EVENT_ADSR,
  EVENT_NOTE_PARAM,
  EVENT_GOTO,
  EVENT_CALL,
  EVENT_RET,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_NOISE_FREQ,
  EVENT_SUBEVENT,
};

enum HeartBeatSnesSeqSubEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  SUBEVENT_UNKNOWN0 = 1,
  SUBEVENT_UNKNOWN1,
  SUBEVENT_UNKNOWN2,
  SUBEVENT_UNKNOWN3,
  SUBEVENT_UNKNOWN4,
  SUBEVENT_LOOP_COUNT,
  SUBEVENT_LOOP_AGAIN,
  SUBEVENT_ADSR_AR,
  SUBEVENT_ADSR_DR,
  SUBEVENT_ADSR_SL,
  SUBEVENT_ADSR_RR,
  SUBEVENT_ADSR_SR,
  SUBEVENT_SURROUND,
};

class HeartBeatSnesSeq
    : public VGMSeq {
 public:
  HeartBeatSnesSeq
      (RawFile *file, HeartBeatSnesVersion ver, u32 seqdataOffset, std::string newName = "HeartBeat SNES Seq");
  ~HeartBeatSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  static double getTempoInBPM(u8 tempo);

  HeartBeatSnesVersion version;
  std::map<u8, HeartBeatSnesSeqEventType> EventMap;
  std::map<u8, HeartBeatSnesSeqSubEventType> SubEventMap;

  static const u8 NOTE_DUR_TABLE[16];
  static const u8 NOTE_VEL_TABLE[16];
  static const u8 PAN_TABLE[22];

 private:
  void loadEventMap();
};


class HeartBeatSnesTrack
    : public SeqTrack {
 public:
  HeartBeatSnesTrack(HeartBeatSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

 private:
  u8 spcNoteDuration;
  u8 spcNoteDurRate;
  u8 spcNoteVolume;
  s16 subReturnOffset;
  u8 loopCount;
  bool slur;
};
