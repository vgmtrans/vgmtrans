/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiTMNT2Format.h"
#include "KonamiTMNT2Instr.h"

#include <vector>
#include <string>

struct konami_tmnt2_instr_info;
class VGMInstr;
enum KonamiTMNT2FormatVer : u8;

class KonamiTMNT2Seq : public VGMSeq {
 public:
  enum SeqType : u8 { ALL_CHANS = 0, RESERVE_CHANS = 1, ALL_CHANS_2 = 2 };

  KonamiTMNT2Seq(RawFile *file,
                 KonamiTMNT2FormatVer fmtVer,
                 uint32_t offset,
                 std::vector<u32> ym2151TrackOffsets,
                 std::vector<u32> k053260TrackOffsets,
                 u8 defaultTickSkipInterval,
                 const std::string &name = std::string("Konami TMNT2 Seq"));

  void resetVars() override;
  bool parseTrackPointers() override;
  void useColl(const VGMColl* coll) override;
  KonamiTMNT2FormatVer fmtVersion() { return m_fmtVer; }

  void setGlobalTranspose(s8 semitones) { m_globalTranspose = semitones; }
  s8 globalTranspose() { return m_globalTranspose; }

  void setMasterAttenuationYM2151(s8 val) { m_masterAttenYM2151 = val; }
  s8 masterAttenuationYM2151() { return m_masterAttenYM2151; }
  void setMasterAttenuationK053260(s8 val) { m_masterAttenK053260 = val; }
  s8 masterAttenuationK053260() { return m_masterAttenK053260; }

  std::optional<konami_tmnt2_instr_info> instrInfo(int idx) {
    if (m_collContext.instrInfos.size() <= idx)
      return std::nullopt;

    return std::optional {m_collContext.instrInfos[idx]};
  }
  std::optional<konami_tmnt2_drum_info> drumInfo(int tableIdx, int keyIdx) {
    if (m_collContext.drumTables.size() <= tableIdx)
      return std::nullopt;
    auto table = m_collContext.drumTables[tableIdx];
    if (table.size() <= keyIdx)
      return std::nullopt;

    return std::optional {table[keyIdx]};
  }

 private:
  struct CollContext {
    std::vector<konami_tmnt2_instr_info> instrInfos;
    std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
  };
  CollContext m_collContext;

  KonamiTMNT2FormatVer m_fmtVer;
  std::vector<u32> m_ym2151TrackOffsets;
  std::vector<u32> m_k053260TrackOffsets;
  u8 m_defaultTickSkipInterval;

  // state
  s8 m_globalTranspose;
  s8 m_masterAttenYM2151;
  s8 m_masterAttenK053260;
};

class KonamiTMNT2Track : public SeqTrack {
 public:
  KonamiTMNT2Track(
    bool isFmTrack,
    KonamiTMNT2Seq *parentSeq,
    uint32_t offset = 0,
    uint32_t length = 0,
    std::string name = "Track"
  );

  double calculateVol(u8 baseVol);
  void handleProgramChangeK053260();
  u8 calculatePan();
  void updatePan();
  void onNoteBegin(int noteDur);
  KonamiTMNT2FormatVer fmtVersion() {
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->fmtVersion();
  }

  void resetVars() override;
  void onTickBegin() override;
  bool readEvent() override;

private:
  std::optional<konami_tmnt2_instr_info> instrInfo(int idx) {
    return dynamic_cast<KonamiTMNT2Seq*>(parentSeq)->instrInfo(idx);
  }
  std::optional<konami_tmnt2_drum_info> drumInfo(int tableIdx, int keyIdx) {
    return dynamic_cast<KonamiTMNT2Seq*>(parentSeq)->drumInfo(tableIdx, keyIdx);
  }

  void setPercussionModeOn() {
    addBankSelectNoItem(1);
    addProgramChangeNoItem(0, false);
    m_state |= 2;
  }
  void setPercussionModeOff() {
    addBankSelectNoItem(0);
    addProgramChangeNoItem(m_program, false);
    handleProgramChangeK053260();
    m_state &= ~2;
  }
  bool percussionMode() const { return (m_state & 2) > 0; }

  s8 globalTranspose() {
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->globalTranspose();
  }

  void setGlobalTranspose(s8 semitones) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setGlobalTranspose(semitones);
  }

  s8 masterAttenuation() {
    if (m_isFmTrack)
      return static_cast<KonamiTMNT2Seq*>(parentSeq)->masterAttenuationYM2151();
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->masterAttenuationK053260();
  }
  void setMasterAttenuationYM2151(s8 val) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setMasterAttenuationYM2151(val);
  }
  void setMasterAttenuationK053260(s8 val) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setMasterAttenuationK053260(val);
  }

  bool m_isFmTrack = false;
  u8 m_program = 0;
  u8 m_state = 0;
  u8 m_rawBaseDur = 0;
  float m_baseDur = 0;
  float m_baseDurHalveBackup = 0;
  u8 m_extendDur = 0;
  u8 m_durSubtract = 0;
  u8 m_noteDurPercent = 0;
  u8 m_baseVol = 0;
  u8 m_dxAtten = 0;
  u8 m_dxAttenMultiplier = 1;
  u8 m_octave = 0;
  s8 m_transpose = 0;
  s8 m_addedToNote = 0;
  u8 m_loopCounter[2];
  u32 m_loopStartOffset[2];
  u8 m_warpCounter = 0;
  u16 m_warpOrigin = 0;
  u16 m_warpDest = 0;
  u16 m_callOrigin[2];

  int m_noteCountdown = 0;

  // LFO-related
  int m_lfoDelay = 0;              // in ticks
  int m_lfoDelayCountdown = 0;     // reset to m_lfoDelay on note on
  int m_lfoRampStepTicks = 0;      // ticks that must elapse before stepping LFO ramp
  int m_lfoRampStepCountdown = 0;  // decremented each tick. At 0, increments m_lfoRampValue and
                                   // resets to m_lfoRampStepTicks.
  u8 m_lfoRampValue = 0;           // PMS/AMS is updated from this every time it is incremented,
                                   // increases until note-off or maximum PMS/AMS is reached

  // k053260-specific state
  u8 m_attenuation = 0;
  u8 m_pan = 0;
  u8 m_instrPan = 0;
};
