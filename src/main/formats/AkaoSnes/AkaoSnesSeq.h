#pragma once

#include "AkaoSnesFormat.h"
#include "automation/SeqMidiAutomation.h"
#include "base/Types.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>
#include <vector>

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
  EVENT_PITCH_ENVELOPE_ON,
  EVENT_PITCH_ENVELOPE_OFF,
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
              u32 seqdataOffset,
              u32 addrAPURelocBase,
              std::string newName = "Square AKAO SNES Seq");
  ~AkaoSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  double getTempoInBPM(u8 tempoValue) const;
  void syncTempoDependentTracks();

  u16 romAddressToApuAddress(u16 romAddress) const;
  u16 getShortAddress(u32 offset) const;

  AkaoSnesVersion version;
  AkaoSnesMinorVersion minorVersion;
  std::map<u8, AkaoSnesSeqEventType> EventMap;

  u8 STATUS_NOTE_MAX;
  u8 STATUS_NOTEINDEX_TIE;
  u8 STATUS_NOTEINDEX_REST;
  std::vector<u8> NOTE_DUR_TABLE;

  u8 TIMER0_FREQUENCY;
  bool PAN_8BIT;
  u8 tempo;

  u32 addrAPURelocBase;
  u32 addrROMRelocBase;
  u32 addrSequenceEnd;

 private:
  void LoadEventMap(void);
};


class AkaoSnesTrack : public SeqTrack {
public:
  AkaoSnesTrack(AkaoSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  void onTickBegin() override;
  bool readEvent() override;
  void syncTempoDependentLfos();

  u16 romAddressToApuAddress(u16 romAddress) const;
  u16 getShortAddress(u32 offset) const;

 private:
  enum class LfoTarget {
    Vibrato,
    Tremolo,
  };

  struct LfoParams {
    u8 delay;
    u8 rate;
    u8 depth;
  };

  LfoParams readLfoParams();
  void applyLfo(LfoTarget target, u32 offset, u32 length, const LfoParams& params);
  void clearLfo(LfoTarget target, u32 offset, u32 length);
  void setLfoOutputDepth(LfoTarget target, u8 depth, bool force);
  void clearLfoRateAndDelay(LfoTarget target);
  void syncLfoRateAndDelay(LfoTarget target);
  void configureVibratoFade();
  void configureTremoloFade();
  void beginVibratoForNote();
  void beginTremoloForNote();
  u8 vibratoFadeDepthMidiValue(s32 depth) const;
  u8 tremoloFadeDepthMidiValue(s32 depth) const;
  void updateVibratoFade();
  void updateTremoloFade();
  void resetPitchState();
  void beginNotePitch(u8 note, bool validForPitchBend);
  void resetPitchBendForNewNote();
  void setPitchEnvelope(s8 semitones, u8 delay, u8 length);
  void clearPitchEnvelope();
  void beginPitchEnvelopeForNote();
  void updatePitchEnvelope();
  bool pitchEnvelopeDelayElapsed();
  bool advancePitchEnvelopeTick(AkaoSnesVersion version, s32& currentOffset);
  void setPendingPitchSlide(u16 steps, s8 semitones);
  void clearPendingPitchSlide();
  void beginPendingPitchSlide();
  void updatePitchSlide();

  u8 onetimeDuration;
  bool slur;
  bool legato;
  bool percussion;
  u8 nonPercussionProgram;
  bool jumpActivatedByMainCpu;

  u8 loopLevel;
  u8 loopIncCount[AKAOSNES_LOOP_LEVEL_MAX];
  u8 loopDecCount[AKAOSNES_LOOP_LEVEL_MAX];
  u16 loopStart[AKAOSNES_LOOP_LEVEL_MAX];

  u8 ignoreMasterVolumeProgNum;

  // Persistent V1/V2 pitch-envelope configuration plus the per-note active
  // ramp state derived from it. V3/V4 pitch slides use the pending fields below
  // because their setup commands are consumed by only the next note or tie.
  struct PitchEnvelopeState {
    bool enabled = false;
    bool active = false;
    s8 semitones = 0;
    u8 delay = 0;
    u8 length = 0;
    u16 progressStep = 0;
    u8 activeDelay = 0;
    u8 activeCount = 0;
    u32 progress = 0;
    s32 targetOffset = 0;
  } pitchEnvelope;

  // Pending one-shot V3/V4 pitch-slide setup. A normal note or tie consumes
  // these fields; rests leave them pending for the next pitch setup path.
  u16 pendingPitchSlideSteps;
  s8 pendingPitchSlideSemitones;
  // V3/V4 slide targets mutate the driver's stored corrected-note byte. The
  // base note anchors MIDI pitch bend to the current sounding note; the current
  // note advances cumulatively through tied slide chains.
  bool pitchSlideNoteValid;
  s16 pitchSlideBaseNote;
  s16 pitchSlideCurrentNote;
  SeqPitchBendAutomation<s32> pitchSlide;
  SeqSynthLfoAutomation vibrato;
  SeqSynthLfoAutomation tremolo;
};
