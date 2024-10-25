#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiArcadeFormat.h"

class KonamiArcadeSeq:
    public VGMSeq {
public:
  KonamiArcadeSeq(RawFile *file, u32 offset, u32 memOffset);

  bool parseHeader() override;
  bool parseTrackPointers() override;

  u32 memOffset() { return m_memOffset; }

private:
  u32 m_memOffset;
};


class KonamiArcadeTrack
    : public SeqTrack {
public:
  KonamiArcadeTrack(KonamiArcadeSeq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  void resetVars() override;
  bool readEvent() override;

private:
  bool bInJump;
  u32 loopMarker[2] = {};
  int loopCounter[2] = {};
  s16 loopAtten[2] = {};
  s16 loopTranspose[2] = {};
  uint8_t prevDelta;
  uint8_t prevDur;
  uint32_t jump_return_offset;
};