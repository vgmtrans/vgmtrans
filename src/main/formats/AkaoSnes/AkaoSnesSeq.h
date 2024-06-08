#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoSnesFormat.h"

#define AKAOSNES_LOOP_LEVEL_MAX 4

enum AkaoSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOTE,
  EVENT_NOP,
  EVENT_NOP1,
  EVENT_VOLUME,
  EVENT_VOLUME_FADE,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_PITCH_SLIDE_ON,
  EVENT_PITCH_SLIDE_OFF,
  EVENT_PITCH_SLIDE,
  EVENT_VIBRATO_ON,
  EVENT_VIBRATO_OFF,
  EVENT_TREMOLO_ON,
  EVENT_TREMOLO_OFF,
  EVENT_PAN_LFO_ON,
  EVENT_PAN_LFO_ON_WITH_DELAY,
  EVENT_PAN_LFO_OFF,
  EVENT_NOISE_FREQ,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_PITCHMOD_ON,
  EVENT_PITCHMOD_OFF,
  EVENT_ECHO_ON,
  EVENT_ECHO_OFF,
  EVENT_OCTAVE,
  EVENT_OCTAVE_UP,
  EVENT_OCTAVE_DOWN,
  EVENT_TRANSPOSE_ABS,
  EVENT_TRANSPOSE_REL,
  EVENT_TUNING,
  EVENT_PROGCHANGE,
  EVENT_VOLUME_ENVELOPE,
  EVENT_GAIN_RELEASE,
  EVENT_DURATION_RATE,
  EVENT_ADSR_AR,
  EVENT_ADSR_DR,
  EVENT_ADSR_SL,
  EVENT_ADSR_SR,
  EVENT_ADSR_DEFAULT,
  EVENT_LOOP_START,
  EVENT_LOOP_END,
  EVENT_SLUR_ON,
  EVENT_SLUR_OFF,
  EVENT_LEGATO_ON,
  EVENT_LEGATO_OFF,
  EVENT_ONETIME_DURATION,
  EVENT_JUMP_TO_SFX_LO,
  EVENT_JUMP_TO_SFX_HI,
  EVENT_END,
  EVENT_TEMPO,
  EVENT_TEMPO_FADE,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_VOLUME_FADE,
  EVENT_ECHO_FEEDBACK_FIR,
  EVENT_MASTER_VOLUME,
  EVENT_LOOP_BREAK,
  EVENT_GOTO,
  EVENT_INC_CPU_SHARED_COUNTER,
  EVENT_ZERO_CPU_SHARED_COUNTER,
  EVENT_ECHO_FEEDBACK_FADE,
  EVENT_ECHO_FIR_FADE,
  EVENT_ECHO_FEEDBACK,
  EVENT_ECHO_FIR,
  EVENT_CPU_CONTROLED_SET_VALUE,
  EVENT_CPU_CONTROLED_JUMP,
  EVENT_CPU_CONTROLED_JUMP_V2,
  EVENT_PERC_ON,
  EVENT_PERC_OFF,
  EVENT_VOLUME_ALT,
  EVENT_IGNORE_MASTER_VOLUME,
  EVENT_IGNORE_MASTER_VOLUME_BROKEN,
  EVENT_LOOP_RESTART,
  EVENT_IGNORE_MASTER_VOLUME_BY_PROGNUM,
  EVENT_PLAY_SFX,
};

class AkaoSnesSeq
    : public VGMSeq {
 public:
  AkaoSnesSeq(RawFile *file,
              AkaoSnesVersion ver,
              AkaoSnesMinorVersion minorVer,
              uint32_t seqdataOffset,
              uint32_t addrAPURelocBase,
              std::string newName = "Square AKAO SNES Seq");
  ~AkaoSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  double getTempoInBPM(uint8_t tempo) const;

  uint16_t romAddressToApuAddress(uint16_t romAddress) const;
  uint16_t getShortAddress(uint32_t offset) const;

  AkaoSnesVersion version;
  AkaoSnesMinorVersion minorVersion;
  std::map<uint8_t, AkaoSnesSeqEventType> EventMap;

  uint8_t STATUS_NOTE_MAX;
  uint8_t STATUS_NOTEINDEX_TIE;
  uint8_t STATUS_NOTEINDEX_REST;
  std::vector<uint8_t> NOTE_DUR_TABLE;

  uint8_t TIMER0_FREQUENCY;
  bool PAN_8BIT;

  uint32_t addrAPURelocBase;
  uint32_t addrROMRelocBase;
  uint32_t addrSequenceEnd;

 private:
  void LoadEventMap(void);
};


class AkaoSnesTrack : public SeqTrack {
public:
  AkaoSnesTrack(AkaoSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  bool readEvent() override;

  uint16_t romAddressToApuAddress(uint16_t romAddress) const;
  uint16_t getShortAddress(uint32_t offset) const;

 private:
  uint8_t onetimeDuration;
  bool slur;
  bool legato;
  bool percussion;
  uint8_t nonPercussionProgram;
  bool jumpActivatedByMainCpu;

  uint8_t loopLevel;
  uint8_t loopIncCount[AKAOSNES_LOOP_LEVEL_MAX];
  uint8_t loopDecCount[AKAOSNES_LOOP_LEVEL_MAX];
  uint16_t loopStart[AKAOSNES_LOOP_LEVEL_MAX];

  uint8_t ignoreMasterVolumeProgNum;
};
