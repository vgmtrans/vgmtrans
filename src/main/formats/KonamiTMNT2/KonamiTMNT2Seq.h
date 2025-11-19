/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiTMNT2Format.h"

#include <vector>
#include <string>

enum KonamiTMNT2FormatVer : uint8_t;

class KonamiTMNT2Seq : public VGMSeq {
 public:
  enum SeqType : u8 { ALL_CHANS = 0, RESERVE_CHANS = 1, ALL_CHANS_2 = 2 };

  KonamiTMNT2Seq(RawFile *file,
                 KonamiTMNT2FormatVer fmtVer,
                 uint32_t offset,
                 std::vector<u32> ym2151TrackOffsets,
                 std::vector<u32> k053260TrackOffsets,
                 const std::string &name = std::string("Konami TMNT2 Seq"));

  void resetVars() override;
  // bool parseHeader() override;
  bool parseTrackPointers() override;

 private:
  KonamiTMNT2FormatVer m_fmtVer;
  std::vector<u32> m_ym2151TrackOffsets;
  std::vector<u32> m_k053260TrackOffsets;
  // std::vector<uint32_t> m_trackOffsets;
};

// class KonamiTMNT2YM2151Track : public SeqTrack {
// public:
//   KonamiTMNT2YM2151Track(KonamiTMNT2Seq *parentSeq, uint32_t offset = 0, uint32_t length = 0);
//
//   void resetVars() override;
//   bool readEvent() override;
// };


class KonamiTMNT2K053260Track : public SeqTrack {
 public:
  KonamiTMNT2K053260Track(KonamiTMNT2Seq *parentSeq, uint32_t offset = 0, uint32_t length = 0);

  void resetVars() override;
  bool readEvent() override;

private:
  void setPercussionModeOn() { m_state |= 2; }
  void setPercussionModeOff() { m_state &= ~2; }
  bool percussionMode() { return (m_state & 2) > 0; }

  u8 m_state = 0;
  u8 m_rawBaseDur = 0;
  u8 m_baseDur = 0;
  u8 m_extendDur = 0;
  u8 m_durSubtract = 0;
  u8 m_attenuation = 0;
  u8 m_octave = 0;
  s8 m_transpose_0 = 0;
  s8 m_transpose_1 = 0;
  s8 m_addedToNote = 0;
  u8 m_dxVal = 1;
  u8 m_loopCounter[2];
  u32 m_loopStartOffset[2];
  u8 m_warpCounter = 0;
  u16 m_warpOrigin = 0;
  u16 m_warpDest = 0;
  // u32 m_callRetOffset = 0;
};
