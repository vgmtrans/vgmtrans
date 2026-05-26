/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "util/types.h"
#include <optional>
#include "VGMSeq.h"
#include "automation/SeqMidiAutomation.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "KonamiSnesFormat.h"

enum KonamiSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_UNKNOWN5,
  EVENT_NOTE,
  EVENT_PERCUSSION_ON,
  EVENT_PERCUSSION_OFF,
  EVENT_GAIN,
  EVENT_INSTANT_TUNING,
  EVENT_REST,
  EVENT_TIE,
  EVENT_PAN,
  EVENT_VIBRATO,
  EVENT_RANDOM_PITCH,
  EVENT_PROGCHANGE,
  EVENT_LOOP_START,
  EVENT_LOOP_END,
  EVENT_LOOP_START_2,
  EVENT_LOOP_END_2,
  EVENT_TEMPO,
  EVENT_TEMPO_FADE_V1,
  EVENT_TEMPO_FADE_V2,
  EVENT_TRANSPABS,
  EVENT_ADSR1,
  EVENT_ADSR2,
  EVENT_VOLUME,
  EVENT_VOLUME_FADE_V1,
  EVENT_VOLUME_FADE_V2,
  EVENT_PORTAMENTO,
  EVENT_PITCH_ENVELOPE_V1,
  EVENT_PITCH_ENVELOPE_V2,
  EVENT_TUNING,
  EVENT_PITCH_SLIDE_V1,
  EVENT_PITCH_SLIDE_V2,
  EVENT_PITCH_SLIDE_V3,
  EVENT_ECHO,
  EVENT_ECHO_PARAM,
  EVENT_LOOP_WITH_VOLTA_START,
  EVENT_LOOP_WITH_VOLTA_END,
  EVENT_PAN_FADE_V1,
  EVENT_PAN_FADE_V2,
  EVENT_VIBRATO_FADE,
  EVENT_ADSR_GAIN,
  EVENT_PROGCHANGEVOL,
  EVENT_CONDITIONAL_JUMP_V1,
  EVENT_LINEAR_PITCH_ENVELOPE_V2,
  EVENT_GOTO,
  EVENT_CALL,
  EVENT_END,
};

class KonamiSnesSeq
    : public VGMSeq {
 public:
  KonamiSnesSeq
      (RawFile *file, KonamiSnesVersion ver, u32 seqdataOffset, std::string newName = "Konami SNES Seq");
  ~KonamiSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  u8 tempo;
  SeqFixedPointAutomation<> tempoFade;
  u32 tempoFadeLastUpdatedTime;
  u8 maxVibratoDepth;
  u16 maxVibratoRateFactor;

  KonamiSnesVersion version;
  std::map<u8, KonamiSnesSeqEventType> EventMap;

  static const u8 PAN_VOLUME_LEFT_V1[];
  static const u8 PAN_VOLUME_RIGHT_V1[];
  static const u8 PAN_VOLUME_LEFT_V2[];
  static const u8 PAN_VOLUME_RIGHT_V2[];
  static const u8 PAN_TABLE[];
  static const u8 VOL_TABLE[];

  u8 NOTE_DUR_RATE_MAX;

  double getTempoInBPM();
  double getTempoInBPM(u8 tempo);

 private:
  void loadEventMap();
};


class KonamiSnesTrack
    : public SeqTrack {
 public:
  KonamiSnesTrack(KonamiSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;
  void onTickBegin() override;

  u8 noteLength;
  u8 noteDurationRate;
  bool inSubroutine;
  u16 subReturnAddr;
  u16 loopReturnAddr;
  u8 loopCount;
  s16 loopVolumeDelta;
  s16 loopPitchDelta;
  u16 loopReturnAddr2;
  u8 loopCount2;
  s16 loopVolumeDelta2;
  s16 loopPitchDelta2;
  u16 voltaLoopStart;
  u16 voltaLoopEnd;
  bool voltaEndMeansPlayFromStart;
  bool voltaEndMeansPlayNextVolta;
  bool percussion;
  u8 instrument;
  s8 prevNoteKey;
  bool prevNoteSlurred;
  double seqTuningCents;

 private:
  struct PitchSlide {
    u32 offset;
    u8 eventLength;
    u8 delay;
    u8 length;
    u8 targetNote;
    double targetSemitones = 0.0;
    s16 delta = 0;
    double deltaSemitones = 0.0;
  };

  struct ControllerFade {
    u32 offset;
    SeqFixedPointMotionPlan<s32> motion;
  };

  std::optional<PitchSlide> consumePitchSlide();
  PitchSlide readPitchSlide(KonamiSnesSeqEventType eventType, u32 offset);
  void addPitchSlideEvent(const PitchSlide& slide);
  void clearActivePitchSlide();
  double noteSemitones(u8 key, bool includeTuning) const;
  void resetPitchForNote(u8 key);
  void beginPitchSlide(const PitchSlide& slide);
  u16 pitchSlideRangeCents(const PitchSlide& slide) const;
  u8 getNoteDuration(u8 length, u8 durationRate) const;
  ControllerFade readVolumeFade(KonamiSnesSeqEventType eventType, u32 offset) const;
  void applyCurrentVolume();
  u8 defaultPanValue() const;
  u8 clampPanValue(u8 pan) const;
  u8 convertPanValueToMidiPan(u8 pan) const;
  ControllerFade readPanFade(KonamiSnesSeqEventType eventType, u32 offset) const;
  void applyCurrentPan();
  ControllerFade readTempoFade(KonamiSnesSeqEventType eventType, u32 offset) const;
  void applyCurrentTempo();
  void syncVibratoRateAndDelay();
  double getTuningInSemitones(s8 tuning);
  u8 convertGAINAmountToGAIN(u8 gainAmount);
  s16 getLoopVolumeDelta() const;
  double getLoopPitchDeltaCents() const;
  void applyEffectiveTuning(u32 offset, u32 length);
  void addUnknownEvent(u32 beginOffset, u8 statusByte, u8 argCount);
  void resetPanAfterProgramChange();
  KonamiSnesSeq& seq();
  const KonamiSnesSeq& seq() const;

  SeqFixedPointAutomation<> panFade;
  SeqFixedPointAutomation<> volumeFade;
  SeqPitchBendAutomation<double> pitchSlide;
  SeqSynthLfoAutomation vibrato;
};
