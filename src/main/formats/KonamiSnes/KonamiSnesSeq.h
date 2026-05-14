/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include <optional>
#include "VGMSeq.h"
#include "SeqMidiAutomation.h"
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
      (RawFile *file, KonamiSnesVersion ver, uint32_t seqdataOffset, std::string newName = "Konami SNES Seq");
  ~KonamiSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  uint8_t tempo;
  vgmtrans::seq::SeqFixedPointAutomation<> tempoFade;
  uint32_t tempoFadeLastUpdatedTime;
  uint8_t maxVibratoDepth;
  uint16_t maxVibratoRateFactor;

  KonamiSnesVersion version;
  std::map<uint8_t, KonamiSnesSeqEventType> EventMap;

  static const uint8_t PAN_VOLUME_LEFT_V1[];
  static const uint8_t PAN_VOLUME_RIGHT_V1[];
  static const uint8_t PAN_VOLUME_LEFT_V2[];
  static const uint8_t PAN_VOLUME_RIGHT_V2[];
  static const uint8_t PAN_TABLE[];
  static const uint8_t VOL_TABLE[];

  uint8_t NOTE_DUR_RATE_MAX;

  double getTempoInBPM();
  double getTempoInBPM(uint8_t tempo);

 private:
  void loadEventMap();
};


class KonamiSnesTrack
    : public SeqTrack {
 public:
  KonamiSnesTrack(KonamiSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  bool readEvent() override;
  void onTickBegin() override;

  uint8_t noteLength;
  uint8_t noteDurationRate;
  bool inSubroutine;
  uint16_t subReturnAddr;
  uint16_t loopReturnAddr;
  uint8_t loopCount;
  int16_t loopVolumeDelta;
  int16_t loopPitchDelta;
  uint16_t loopReturnAddr2;
  uint8_t loopCount2;
  int16_t loopVolumeDelta2;
  int16_t loopPitchDelta2;
  uint16_t voltaLoopStart;
  uint16_t voltaLoopEnd;
  bool voltaEndMeansPlayFromStart;
  bool voltaEndMeansPlayNextVolta;
  bool percussion;
  uint8_t instrument;
  int8_t prevNoteKey;
  bool prevNoteSlurred;
  double seqTuningCents;

 private:
  struct PitchSlide {
    uint32_t offset;
    uint8_t eventLength;
    uint8_t delay;
    uint8_t length;
    uint8_t targetNote;
    double targetSemitones = 0.0;
    int16_t delta = 0;
    double deltaSemitones = 0.0;
  };

  struct ControllerFade {
    uint32_t offset;
    vgmtrans::seq::SeqMotionPlan<int32_t> motion;
  };

  std::optional<PitchSlide> consumePitchSlide();
  PitchSlide readPitchSlide(KonamiSnesSeqEventType eventType, uint32_t offset);
  void addPitchSlideEvent(const PitchSlide& slide);
  void clearActivePitchSlide();
  double noteSemitones(uint8_t key, bool includeTuning) const;
  void resetPitchForNote(uint8_t key);
  void beginPitchSlide(const PitchSlide& slide);
  uint16_t pitchSlideRangeCents(const PitchSlide& slide) const;
  uint8_t getNoteDuration(uint8_t length, uint8_t durationRate) const;
  ControllerFade readVolumeFade(KonamiSnesSeqEventType eventType, uint32_t offset) const;
  void addVolumeFadeEvent(const ControllerFade& fade);
  void clearActiveVolumeFade();
  void applyCurrentVolume();
  void beginVolumeFade(const ControllerFade& fade);
  uint8_t defaultPanValue() const;
  uint8_t clampPanValue(uint8_t pan) const;
  uint8_t convertPanValueToMidiPan(uint8_t pan) const;
  ControllerFade readPanFade(KonamiSnesSeqEventType eventType, uint32_t offset) const;
  void addPanFadeEvent(const ControllerFade& fade);
  void beginPanFade(const ControllerFade& fade);
  void clearActivePanFade();
  void applyCurrentPan();
  ControllerFade readTempoFade(KonamiSnesSeqEventType eventType, uint32_t offset) const;
  void addTempoFadeEvent(const ControllerFade& fade);
  void beginTempoFade(const ControllerFade& fade);
  void clearActiveTempoFade();
  void applyCurrentTempo();
  void syncVibratoRateAndDelay();
  double getTuningInSemitones(int8_t tuning);
  uint8_t convertGAINAmountToGAIN(uint8_t gainAmount);
  int16_t getLoopVolumeDelta() const;
  double getLoopPitchDeltaCents() const;
  void applyEffectiveTuning(uint32_t offset, uint32_t length);
  void addUnknownEvent(uint32_t beginOffset, uint8_t statusByte, uint8_t argCount);
  void resetPanAfterProgramChange();
  KonamiSnesSeq& seq();
  const KonamiSnesSeq& seq() const;

  vgmtrans::seq::SeqFixedPointAutomation<> panFade;
  vgmtrans::seq::SeqFixedPointAutomation<> volumeFade;
  vgmtrans::seq::SeqPitchBendAutomation<double> pitchSlide;
  vgmtrans::seq::SeqSynthLfoAutomation vibrato;
};
