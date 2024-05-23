#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "PrismSnesFormat.h"

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
  PrismSnesSeq(RawFile *file, PrismSnesVersion ver, uint32_t seqdataOffset, std::string newName = "I'Max SNES Seq");
  virtual ~PrismSnesSeq(void);

  virtual bool GetHeaderInfo(void);
  virtual bool GetTrackPointers(void);
  virtual void ResetVars(void);

  PrismSnesVersion version;
  std::map<uint8_t, PrismSnesSeqEventType> EventMap;

  VGMHeader *envContainer;
  void DemandEnvelopeContainer(uint32_t offset);

  static const uint8_t PAN_TABLE_1[21];
  static const uint8_t PAN_TABLE_2[21];

  bool conditionSwitch;

  double GetTempoInBPM(uint8_t tempo);

 private:
  void LoadEventMap(void);
};


class PrismSnesTrack
    : public SeqTrack {
 public:
  PrismSnesTrack(PrismSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  virtual void ResetVars(void);
  virtual bool ReadEvent(void);

  std::vector<uint8_t> panTable;

 private:
  uint8_t defaultLength;
  bool slur; // bit $01
  bool manualDuration; // bit $10
  bool prevNoteSlurred; // bit $20
  int8_t prevNoteKey;
  uint8_t autoDurationThreshold;
  uint8_t spcVolume;
  uint8_t loopCount;
  uint8_t loopCountAlt;
  uint16_t subReturnAddr;

  bool ReadDeltaTime(uint32_t &curOffset, uint8_t &len);
  bool ReadDuration(uint32_t &curOffset, uint8_t len, uint8_t &durDelta);
  uint8_t GetDuration(uint32_t curOffset, uint8_t len, uint8_t durDelta);

  void AddVolumeEnvelope(uint16_t envelopeAddress);
  void AddPanEnvelope(uint16_t envelopeAddress);
  void AddEchoVolumeEnvelope(uint16_t envelopeAddress);
  void AddGAINEnvelope(uint16_t envelopeAddress);
  void AddPanTable(uint16_t panTableAddress);
};
