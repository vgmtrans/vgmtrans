#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoFormat.h"
#include "Matcher.h"

class AkaoInstrSet;

enum class AkaoPs1Version : uint8_t {
  // Final Fantasy 7
  // SaGa Frontier
  // Front Mission 2
  // Chocobo's Mysterious Dungeon
  // Parasite Eve
  VERSION_1,

  VERSION_2
};

enum AkaoSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNIMPLEMENTED = 1,
  EVENT_END,
  EVENT_PROGCHANGE,
  EVENT_ONE_TIME_DURATION,
  EVENT_VOLUME,
  EVENT_PITCH_SLIDE,
  EVENT_OCTAVE,
  EVENT_INCREMENT_OCTAVE,
  EVENT_DECREMENT_OCTAVE,
  EVENT_EXPRESSION,
  EVENT_EXPRESSION_FADE,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_NOISE_CLOCK,
  EVENT_ADSR_ATTACK_RATE,
  EVENT_ADSR_DECAY_RATE,
  EVENT_ADSR_SUSTAIN_LEVEL,
  EVENT_ADSR_DECAY_RATE_AND_SUSTAIN_LEVEL,
  EVENT_ADSR_SUSTAIN_RATE,
  EVENT_ADSR_RELEASE_RATE,
  EVENT_RESET_ADSR,
  EVENT_VIBRATO,
  EVENT_VIBRATO_DEPTH,
  EVENT_VIBRATO_OFF,
  EVENT_ADSR_ATTACK_MODE,
  EVENT_TREMOLO,
  EVENT_TREMOLO_DEPTH,
  EVENT_TREMOLO_OFF,
  EVENT_ADSR_SUSTAIN_MODE,
  EVENT_PAN_LFO,
  EVENT_PAN_LFO_DEPTH,
  EVENT_PAN_LFO_OFF,
  EVENT_ADSR_RELEASE_MODE,
  EVENT_TRANSPOSE_ABS,
  EVENT_TRANSPOSE_REL,
  EVENT_REVERB_ON,
  EVENT_REVERB_OFF,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_PITCH_MOD_ON,
  EVENT_PITCH_MOD_OFF,
  EVENT_LOOP_START,
  EVENT_LOOP_UNTIL,
  EVENT_LOOP_AGAIN,
  EVENT_RESET_VOICE_EFFECTS,
  EVENT_SLUR_ON,
  EVENT_SLUR_OFF,
  EVENT_NOISE_ON_DELAY_TOGGLE,
  EVENT_NOISE_DELAY_TOGGLE,
  EVENT_LEGATO_ON,
  EVENT_LEGATO_OFF,
  EVENT_PITCH_MOD_ON_DELAY_TOGGLE,
  EVENT_PITCH_MOD_DELAY_TOGGLE,
  EVENT_D4,
  EVENT_D5,
  EVENT_D6,
  EVENT_D7,
  EVENT_TUNING_ABS,
  EVENT_TUNING_REL,
  EVENT_PORTAMENTO_ON,
  EVENT_PORTAMENTO_OFF,
  EVENT_FIXED_DURATION,
  EVENT_VIBRATO_DEPTH_FADE,
  EVENT_TREMOLO_DEPTH_FADE,
  EVENT_PAN_LFO_FADE,
  EVENT_E0_V2,
  EVENT_E1_V2,
  EVENT_E2_V2,
  EVENT_E4_V2,
  EVENT_E5_V2,
  EVENT_E6_V2,
  EVENT_TEMPO,
  EVENT_TEMPO_FADE,
  EVENT_REVERB_DEPTH,
  EVENT_REVERB_DEPTH_FADE,
  EVENT_DRUM_ON_V1,
  EVENT_DRUM_ON_V2,
  EVENT_DRUM_OFF,
  EVENT_UNCONDITIONAL_JUMP,
  EVENT_CPU_CONDITIONAL_JUMP,
  EVENT_LOOP_BRANCH,
  EVENT_LOOP_BREAK,
  EVENT_PROGCHANGE_NO_ATTACK,
  EVENT_F3_V1,
  EVENT_UNISON_ON,
  EVENT_UNISON_OFF,
  EVENT_UNISON_VOLUME_BALANCE,
  EVENT_UNISON_VOLUME_BALANCE_FADE,
  EVENT_FE_0A_V2,
  EVENT_FE_0B_V2,
  EVENT_FE_0E_V2,
  EVENT_FE_0F_V2,
  EVENT_FE_10_V2,
  EVENT_FE_11_V2,
  EVENT_FE_12_V2,
  EVENT_FE_14_V2,
  EVENT_ALTERNATE_VOICE_ON,
  EVENT_ALTERNATE_VOICE_OFF,
  EVENT_TIME_SIGNATURE,
  EVENT_MEASURE,
  EVENT_FE_19_V2,
  EVENT_FE_1A_V2,
  EVENT_FE_1B_V2,
  EVENT_FE_1C_V2,
  EVENT_FE_1D_V2,
  EVENT_FE_1E_V2
};

class AkaoSeq:
    public VGMSeq {
 public:
  AkaoSeq(RawFile *file, uint32_t offset);
  virtual ~AkaoSeq();

  virtual bool GetHeaderInfo() override;
  virtual bool GetTrackPointers() override;

  std::wstring ReadTimestampAsText();
  double GetTempoInBPM(uint16_t tempo) const;

  static AkaoPs1Version GuessVersion(RawFile *file, uint32_t offset);
  static uint32_t ReadNumOfTracks(RawFile *file, uint32_t offset);

 private:
  void LoadEventMap();

  static uint8_t GetNumPositiveBits(uint32_t ulWord) {
    return   ((ulWord & 0x80000000)>0) + ((ulWord & 0x40000000)>0) + ((ulWord & 0x20000000)>0) + ((ulWord & 0x10000000)>0)
      + ((ulWord & 0x8000000)>0) + ((ulWord & 0x4000000)>0) + ((ulWord & 0x2000000)>0) + ((ulWord & 0x1000000)>0)
      + ((ulWord & 0x800000)>0) + ((ulWord & 0x400000)>0) + ((ulWord & 0x200000)>0) + ((ulWord & 0x100000)>0)
      + ((ulWord & 0x80000)>0) + ((ulWord & 0x40000)>0) + ((ulWord & 0x20000)>0) + ((ulWord & 0x10000)>0)
      + ((ulWord & 0x8000)>0) + ((ulWord & 0x4000)>0) + ((ulWord & 0x2000)>0) + ((ulWord & 0x1000)>0)
      + ((ulWord & 0x800)>0) + ((ulWord & 0x400)>0) + ((ulWord & 0x200)>0) + ((ulWord & 0x100)>0)
      + ((ulWord & 0x80)>0) + ((ulWord & 0x40)>0) + ((ulWord & 0x20)>0) + ((ulWord & 0x10)>0)
      + ((ulWord & 0x8)>0) + ((ulWord & 0x4)>0) + ((ulWord & 0x2)>0) + ((ulWord & 0x1));
  }

 public:
  AkaoInstrSet *instrset;
  uint16_t seq_id;
  AkaoPs1Version version;

  std::map<uint8_t, AkaoSeqEventType> event_map;
  std::map<uint8_t, AkaoSeqEventType> sub_event_map;

  bool bUsesIndividualArts;
};

class AkaoTrack
    : public SeqTrack {
 public:
  AkaoTrack(AkaoSeq *parentFile, long offset = 0, long length = 0);

  virtual void ResetVars() override;
  virtual bool ReadEvent() override;

  AkaoSeq * seq() const {
    return reinterpret_cast<AkaoSeq*>(this->parentSeq);
  }

 public:


 protected:
  bool slur;
  bool legato;
  uint32_t loop_begin_loc[4];
  uint16_t loop_layer;
  uint16_t loop_counter[4];
  uint16_t last_delta_time;
  bool use_one_time_delta_time;
  uint8_t one_time_delta_time;
  uint16_t delta_time_overwrite;
  int8_t tuning;
};
