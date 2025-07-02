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

  bool parseHeader() override;
  bool parseTrackPointers() override;

  u32 memOffset() { return m_memOffset; }
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums() { return m_drums; }
  float nmiRate() { return m_nmiRate; }

public:
  KonamiArcadeFormatVer fmtVer;

private:
  u32 m_memOffset;
  const std::array<KonamiArcadeInstrSet::drum, 46> m_drums;
  float m_nmiRate;
};


class KonamiArcadeTrack
    : public SeqTrack {
public:
  KonamiArcadeTrack(KonamiArcadeSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  void resetVars() override;
  bool readEvent() override;

private:
  void makeTrulyPrevDurNoteEnd(uint32_t absTime) const;

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
  double m_timePerTickMicroseconds;
  bool m_portamentoEnabled;
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
  u8 m_vol;
  u8 m_pan;
  u32 m_subroutineOffset;
  u32 m_subroutineReturnOffset;
  bool m_needsSubroutineEnd = false;
  bool m_inSubRoutine = false;
  u32 m_jumpReturnOffset;
};