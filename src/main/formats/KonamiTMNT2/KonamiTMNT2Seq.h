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
enum KonamiTMNT2FormatVer : uint8_t;

class KonamiTMNT2Seq : public VGMSeq {
 public:
  enum SeqType : uint8_t { ALL_CHANS = 0, RESERVE_CHANS = 1, ALL_CHANS_2 = 2 };

  KonamiTMNT2Seq(RawFile *file,
                 KonamiTMNT2FormatVer fmtVer,
                 uint32_t offset,
                 std::vector<uint32_t> ym2151TrackOffsets,
                 std::vector<uint32_t> k053260TrackOffsets,
                 uint8_t defaultTickSkipInterval,
                 uint8_t clkb,
                 const std::string &name = std::string("Konami TMNT2 Seq"));

  void resetVars() override;
  bool parseTrackPointers() override;
  void useColl(const VGMColl* coll) override;
  KonamiTMNT2FormatVer fmtVersion() { return m_fmtVer; }
  uint8_t clkb() { return m_clkb; }

  void setGlobalTranspose(int8_t semitones) { m_globalTranspose = semitones; }
  int8_t globalTranspose() { return m_globalTranspose; }

  void setMasterAttenuationYM2151(int8_t val) { m_masterAttenYM2151 = val; }
  int8_t masterAttenuationYM2151() { return m_masterAttenYM2151; }
  void setMasterAttenuationK053260(int8_t val) { m_masterAttenK053260 = val; }
  int8_t masterAttenuationK053260() { return m_masterAttenK053260; }

  std::optional<konami_tmnt2_instr_info> instrInfo(int idx) {
    if (m_collContext.instrInfos.size() <= idx)
      return std::nullopt;

    return std::optional {m_collContext.instrInfos[idx]};
  }
  std::optional<konami_tmnt2_drum_info> drumInfo(int tableIdx, int keyIdx) {
    uint8_t key = (tableIdx * 16) + keyIdx;
    if (!m_collContext.drumKeyMap.contains(key))
      return std::nullopt;
    return m_collContext.drumKeyMap[key];
  }

 private:
  struct CollContext {
    std::vector<konami_tmnt2_instr_info> instrInfos;
    std::unordered_map<uint8_t, konami_tmnt2_drum_info> drumKeyMap;
  };
  CollContext m_collContext;

  KonamiTMNT2FormatVer m_fmtVer;
  std::vector<uint32_t> m_ym2151TrackOffsets;
  std::vector<uint32_t> m_k053260TrackOffsets;
  uint8_t m_defaultTickSkipInterval;
  uint8_t m_clkb;

  // state
  int8_t m_globalTranspose;
  int8_t m_masterAttenYM2151;
  int8_t m_masterAttenK053260;
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

  double calculateVol(uint8_t baseVol);
  void handleProgramChangeK053260();
  uint8_t calculatePan();
  void updatePan();
  void onNoteBegin(int noteDur);
  KonamiTMNT2FormatVer fmtVersion() {
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->fmtVersion();
  }
  uint8_t clkb() {
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->clkb();
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

  int8_t globalTranspose() {
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->globalTranspose();
  }

  void setGlobalTranspose(int8_t semitones) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setGlobalTranspose(semitones);
  }

  int8_t masterAttenuation() {
    if (m_isFmTrack)
      return static_cast<KonamiTMNT2Seq*>(parentSeq)->masterAttenuationYM2151();
    return static_cast<KonamiTMNT2Seq*>(parentSeq)->masterAttenuationK053260();
  }
  void setMasterAttenuationYM2151(int8_t val) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setMasterAttenuationYM2151(val);
  }
  void setMasterAttenuationK053260(int8_t val) {
    static_cast<KonamiTMNT2Seq*>(parentSeq)->setMasterAttenuationK053260(val);
  }

  bool m_isFmTrack = false;
  uint8_t m_program = 0;
  uint8_t m_state = 0;
  uint8_t m_rawBaseDur = 0;
  float m_baseDur = 0;
  float m_baseDurHalveBackup = 0;
  uint8_t m_extendDur = 0;
  uint8_t m_durSubtract = 0;
  uint8_t m_noteDurPercent = 0;
  uint8_t m_baseVol = 0;
  uint8_t m_dxAtten = 0;
  uint8_t m_dxAttenMultiplier = 1;
  uint8_t m_noteOffset = 0;
  int8_t m_transpose = 0;
  int8_t m_addedToNote = 0;
  uint8_t m_drumBank = 0;
  uint8_t m_loopCounter[2];
  uint32_t m_loopStartOffset[2];
  uint8_t m_warpCounter = 0;
  uint16_t m_warpOrigin = 0;
  uint16_t m_warpDest = 0;
  uint16_t m_callOrigin[2];

  int m_noteCountdown = 0;

  // LFO-related
  int m_lfoDelay = 0;              // in ticks
  int m_lfoDelayCountdown = 0;     // reset to m_lfoDelay on note on
  int m_lfoRampStepTicks = 0;      // ticks that must elapse before stepping LFO ramp
  int m_lfoRampStepCountdown = 0;  // decremented each tick. At 0, increments m_lfoRampValue and
                                   // resets to m_lfoRampStepTicks.
  uint8_t m_lfoRampValue = 0;           // PMS/AMS is updated from this every time it is incremented,
                                   // increases until note-off or maximum PMS/AMS is reached

  // k053260-specific state
  uint8_t m_attenuation = 0;
  uint8_t m_pan = 0;
  uint8_t m_instrPan = 0;
};
