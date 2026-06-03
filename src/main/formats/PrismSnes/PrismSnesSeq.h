#pragma once

#include "base/Types.h"
#include "PrismSnesFormat.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>
#include <vector>

enum PrismSnesSeqEventType {
  EVENT_UNKNOWN0 =
  1, //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_PITCH_SLIDE,
  EVENT_UNKNOWN_EVENT_ED,
  EVENT_TIE_WITH_DUR,
  EVENT_TIE,
  EVENT_NOP2,
  EVENT_NOTE,
  EVENT_NOISE_NOTE,
  EVENT_TEMPO,
  EVENT_CONDITIONAL_JUMP,
  EVENT_CONDITION,
  EVENT_RESTORE_ECHO_PARAM,
  EVENT_SAVE_ECHO_PARAM,
  EVENT_SLUR_OFF,
  EVENT_SLUR_ON,
  EVENT_VOLUME_ENVELOPE,
  EVENT_DEFAULT_PAN_TABLE_1,
  EVENT_DEFAULT_PAN_TABLE_2,
  EVENT_INC_APU_PORT_3,
  EVENT_INC_APU_PORT_2,
  EVENT_PLAY_SONG_3,
  EVENT_PLAY_SONG_2,
  EVENT_PLAY_SONG_1,
  EVENT_TRANSPOSE_REL,
  EVENT_PAN_ENVELOPE,
  EVENT_PAN_TABLE,
  EVENT_DEFAULT_LENGTH_OFF,
  EVENT_DEFAULT_LENGTH,
  EVENT_LOOP_UNTIL,
  EVENT_LOOP_UNTIL_ALT,
  EVENT_RET,
  EVENT_CALL,
  EVENT_GOTO,
  EVENT_TRANSPOSE,
  EVENT_TUNING,
  EVENT_VIBRATO_DELAY,
  EVENT_VIBRATO_OFF,
  EVENT_VIBRATO,
  EVENT_VOLUME_REL,
  EVENT_PAN,
  EVENT_VOLUME,
  EVENT_REST,
  EVENT_GAIN_ENVELOPE_REST,
  EVENT_GAIN_ENVELOPE_DECAY_TIME,
  EVENT_MANUAL_DURATION_OFF,
  EVENT_MANUAL_DURATION_ON,
  EVENT_AUTO_DURATION_THRESHOLD,
  EVENT_GAIN_ENVELOPE_SUSTAIN,
  EVENT_ECHO_VOLUME_ENVELOPE,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_OFF,
  EVENT_ECHO_ON,
  EVENT_ECHO_PARAM,
  EVENT_ADSR,
  EVENT_GAIN_ENVELOPE_DECAY,
  EVENT_INSTRUMENT,
  EVENT_END,
};

class PrismSnesSeq
    : public VGMSeq {
 public:
  PrismSnesSeq(RawFile *file, PrismSnesVersion ver, u32 seqdataOffset, std::string name = "I'Max SNES Seq");
  virtual ~PrismSnesSeq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

  PrismSnesVersion version;
  std::map<u8, PrismSnesSeqEventType> EventMap;

  VGMHeader *envContainer;
  void demandEnvelopeContainer(u32 offset);

  static const u8 PAN_TABLE_1[21];
  static const u8 PAN_TABLE_2[21];

  bool conditionSwitch;

  double getTempoInBPM(u8 tempo);

 private:
  void loadEventMap();
};


class PrismSnesTrack
    : public SeqTrack {
 public:
  PrismSnesTrack(PrismSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  virtual void resetVars();
  virtual bool readEvent();

  std::vector<u8> panTable;

 private:
  u8 defaultLength;
  bool slur; // bit $01
  bool manualDuration; // bit $10
  bool prevNoteSlurred; // bit $20
  s8 prevNoteKey;
  u8 autoDurationThreshold;
  u8 spcVolume;
  u8 loopCount;
  u8 loopCountAlt;
  u16 subReturnAddr;

  bool readDeltaTime(u32 &curOffset, u8 &len);
  bool readDuration(u32 &curOffset, u8 len, u8 &durDelta);
  u8 getDuration(u32 curOffset, u8 len, u8 durDelta);

  void addVolumeEnvelope(u16 envelopeAddress);
  void addPanEnvelope(u16 envelopeAddress);
  void addEchoVolumeEnvelope(u16 envelopeAddress);
  void addGAINEnvelope(u16 envelopeAddress);
  void addPanTable(u16 panTableAddress);
};
