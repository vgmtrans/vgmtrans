/*
* VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include "VGMInstrSet.h"
#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2OPMInstr.h"

class RawFile;
class VGMItem;
enum KonamiTMNT2FormatVer : uint8_t;

struct konami_vendetta_instr_k053260 {
  u8 samp_info_idx;
  u8 attenuation;
  u8 unknown;
};

struct konami_vendetta_sample_info {
  u8 pitch_lo;
  u8 pitch_hi;
  u8 length_lo;
  u8 length_hi;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 is_kapdcm;

  constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr u32 length() const noexcept {
    return (length_hi << 8) | length_lo;
  }

  constexpr bool isAdpcm() const noexcept { return is_kapdcm; }
  constexpr bool isReverse() const noexcept { return false; }
};

struct konami_vendetta_drum_info {
  konami_vendetta_instr_k053260 instr;
  s8 pan = -1;
  s16 pitch = -1;
};

struct vendetta_sub_offsets {
  u16 load_instr;
  u16 set_pan;
  u16 set_pitch;
  u16 note_on;
};


class KonamiVendettaSampleInstrSet
    : public VGMInstrSet {
public:

  KonamiVendettaSampleInstrSet(RawFile *file,
                               u32 offset,
                               u32 instrTableOffsetYM2151,
                               u32 instrTableOffsetK053260,
                               u32 sampInfoTableOffset,
                               u32 drumBanksOffset,
                               std::map<u16, int> drumOffsetToIdx,
                               const std::vector<konami_vendetta_instr_k053260>& instrs,
                               const std::vector<konami_vendetta_sample_info>& sampInfos,
                               const std::vector<konami_vendetta_drum_info>& drumInfos,
                               std::string name,
                               KonamiTMNT2FormatVer fmtVer);
  ~KonamiVendettaSampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, u32 off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  const std::vector<konami_vendetta_instr_k053260> instrsK053260() { return m_instrsK053260; }
  const std::vector<konami_tmnt2_ym2151_instr> instrsYM2151() { return m_instrsYM2151; }
  const std::vector<konami_vendetta_sample_info> sampleInfos() { return m_sampInfos; }
  const std::vector<konami_vendetta_drum_info> drumInfos() { return m_drumInfos; }
  const std::unordered_map<u8, konami_vendetta_drum_info> drumKeyMap() { return m_drumKeyMap; }

private:
  u32 m_instrTableOffsetYM2151;
  u32 m_instrTableOffsetK053260;
  u32 m_sampInfoTableOffset;
  u32 m_drumBanksOffset;
  std::map<u16, int> m_drumOffsetToIdx;
  std::vector<konami_tmnt2_ym2151_instr> m_instrsYM2151;
  std::vector<konami_vendetta_instr_k053260> m_instrsK053260;
  std::vector<konami_vendetta_sample_info> m_sampInfos;
  std::vector<konami_vendetta_drum_info> m_drumInfos;
  std::unordered_map<u8, konami_vendetta_drum_info> m_drumKeyMap;

  KonamiTMNT2FormatVer m_fmtVer;
};
