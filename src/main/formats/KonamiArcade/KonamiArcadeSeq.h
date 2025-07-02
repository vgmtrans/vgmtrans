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
    u32 offset,
    u32 memOffset,
    const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
    float nmiRate,
    const std::string& name
  );
  void resetVars() override;
  bool parseHeader() override;
  bool parseTrackPointers() override;

  u32 memOffset() { return m_memOffset; }
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums() { return m_drums; }
  float nmiRate() { return m_nmiRate; }

public:
  KonamiArcadeFormatVer fmtVer;
  u8 getTempoSlideDuration() { return m_tempoSlideDuration; }
  void setTempoSlideDuration(u8 dur) { m_tempoSlideDuration = dur; }
  double getTempoSlideIncrement() { return m_tempoSlideIncrement; }
  void setTempoSlideIncrement(double increment) { m_tempoSlideIncrement = increment; }

private:
  u32 m_memOffset;
  const std::array<KonamiArcadeInstrSet::drum, 46> m_drums;
  float m_nmiRate;

  u8 m_tempoSlideDuration;
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

  u8 calculateMidiPanForK054539(u8 pan);
  void enablePercussion(bool& flag);
  void disablePercussion(bool& flag);
  void applyTranspose();

  bool m_inJump;
  bool m_percussionFlag1;
  bool m_percussionFlag2;
  u32 m_prevNoteAbsTime;
  u32 m_prevNoteDur;
  u32 m_prevNoteDelta;
  u8 m_prevFinalKey;
  double m_tempo;
  double m_microsecsPerTick;

  u8 m_volSlideDuration;
  s16 m_volSlideIncrement;
  u8 m_panSlideDuration;
  s16 m_panSlideIncrement;
  u8 m_portamentoTime;
  u8 m_slideModeDelay;
  u8 m_slideModeDuration;
  s8 m_slideModeDepth;
  s8 m_driverTranspose;
  u8 m_releaseRate;
  u8 m_curProg;
  u32 m_loopMarker[2] = {};
  int m_loopCounter[2] = {};
  s16 m_loopAtten[2] = {};
  s16 m_loopTranspose[2] = {};
  u8 m_prevDelta;
  u8 m_duration;
  s16 m_vol;
  u8 m_pan;
  s16 m_actualPan;
  u32 m_subroutineOffset;
  u32 m_subroutineReturnOffset;
  bool m_needsSubroutineEnd = false;
  bool m_inSubRoutine = false;
  u32 m_jumpReturnOffset;
};