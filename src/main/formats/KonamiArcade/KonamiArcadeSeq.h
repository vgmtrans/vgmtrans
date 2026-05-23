#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiArcadeFormat.h"

class KonamiArcadeSeq:
    public VGMSeq {
public:
  KonamiArcadeSeq(
    RawFile *file,
    KonamiArcadeFormatVer fmt_version,
    uint32_t offset,
    uint32_t memOffset,
    const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
    float nmiRate,
    const std::string& name
  );
  void resetVars() override;
  bool parseHeader() override;
  bool parseTrackPointers() override;

  uint32_t memOffset() { return m_memOffset; }
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums() { return m_drums; }
  float nmiRate() { return m_nmiRate; }

public:
  KonamiArcadeFormatVer fmtVer;
  uint8_t getTempoSlideDuration() { return m_tempoSlideDuration; }
  void setTempoSlideDuration(uint8_t dur) { m_tempoSlideDuration = dur; }
  double getTempoSlideIncrement() { return m_tempoSlideIncrement; }
  void setTempoSlideIncrement(double increment) { m_tempoSlideIncrement = increment; }

private:
  uint32_t m_memOffset;
  const std::array<KonamiArcadeInstrSet::drum, 46> m_drums;
  float m_nmiRate;

  uint8_t m_tempoSlideDuration;
  double m_tempoSlideIncrement;
};


class KonamiArcadeTrack
    : public SeqTrack {
public:
  KonamiArcadeTrack(KonamiArcadeSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  void resetVars() override;
  bool readEvent() override;
  void onTickBegin() override;

private:
  void makeTrulyPrevDurNoteEnd(uint32_t absTime) const;
  std::pair<double, double> calculateTempo(double tempoByte);

  uint8_t calculateMidiPanForK054539(uint8_t pan);
  void enablePercussion(bool& flag);
  void disablePercussion(bool& flag);
  bool percussionEnabled();
  void applyTranspose();

  bool m_inJump;
  bool m_percussionFlag1;
  bool m_percussionFlag2;
  uint32_t m_prevNoteAbsTime;
  uint32_t m_prevNoteDur;
  uint32_t m_prevNoteDelta;
  uint8_t m_prevFinalKey;
  bool m_tiePrevNote;
  bool m_didCancelDurTie;
  double m_tempo;
  double m_microsecsPerTick;

  uint8_t m_volSlideDuration;
  int16_t m_volSlideTarget;
  int16_t m_volSlideIncrement;
  uint8_t m_panSlideDuration;
  int16_t m_panSlideTarget;
  int16_t m_panSlideIncrement;
  uint8_t m_portamentoTime;
  uint8_t m_slideModeDelay;
  uint8_t m_slideModeDuration;
  int8_t m_slideModeDepth;
  int8_t m_driverTranspose;
  uint8_t m_releaseRate;
  uint8_t m_curProg;
  uint32_t m_loopMarker[2] = {};
  int m_loopCounter[2] = {};
  int16_t m_loopAtten[2] = {};
  int16_t m_loopTranspose[2] = {};
  uint8_t m_prevDelta;
  uint8_t m_duration;
  int16_t m_vol;
  uint8_t m_pan;
  int16_t m_actualPan;
  uint32_t m_subroutineOffset;
  uint32_t m_subroutineReturnOffset;
  bool m_needsSubroutineEnd = false;
  bool m_inSubRoutine = false;
  uint32_t m_jumpReturnOffset;
};