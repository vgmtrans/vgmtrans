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
#include <unordered_map>

class RawFile;
class VGMItem;
enum KonamiTMNT2FormatVer : uint8_t;

struct konami_vendetta_instr_k053260 {
  uint8_t samp_info_idx;
  uint8_t attenuation;
  uint8_t unknown;
};

struct konami_vendetta_sample_info {
  uint8_t pitch_lo;
  uint8_t pitch_hi;
  uint8_t length_lo;
  uint8_t length_hi;
  uint8_t start_lo;
  uint8_t start_mid;
  uint8_t start_hi;
  uint8_t is_kapdcm;

  constexpr uint32_t start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr uint32_t length() const noexcept {
    return (length_hi << 8) | length_lo;
  }

  constexpr bool isAdpcm() const noexcept { return is_kapdcm; }
  constexpr bool isReverse() const noexcept { return false; }
};

struct konami_vendetta_drum_info {
  konami_vendetta_instr_k053260 instr;
  int8_t pan = -1;
  int16_t pitch = -1;
};

struct vendetta_sub_offsets {
  uint32_t load_instr;
  uint32_t set_pan;
  uint32_t set_pitch;
  uint32_t note_on;
};


class KonamiVendettaSampleInstrSet
    : public VGMInstrSet {
public:

  KonamiVendettaSampleInstrSet(RawFile *file,
                               uint32_t offset,
                               uint32_t instrTableOffsetYM2151,
                               uint32_t instrTableOffsetK053260,
                               uint32_t sampInfosOffset,
                               uint32_t drumBanksOffset,
                               uint32_t drumsOffset,
                               vendetta_sub_offsets subOffsets,
                               const std::vector<konami_vendetta_instr_k053260>& instrs,
                               const std::vector<konami_vendetta_sample_info>& sampInfos,
                               std::string name,
                               KonamiTMNT2FormatVer fmtVer);
  ~KonamiVendettaSampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, uint32_t off);
  void addSampleInfoItems();
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  konami_vendetta_drum_info parseVendettaDrum(
    RawFile* programRom,
    uint16_t& offset,
    const vendetta_sub_offsets& subOffsets,
    VGMItem* drumItem
  );

  const std::vector<konami_vendetta_instr_k053260> instrsK053260() { return m_instrsK053260; }
  const std::vector<konami_tmnt2_ym2151_instr> instrsYM2151() { return m_instrsYM2151; }
  const std::vector<konami_vendetta_sample_info> sampleInfos() { return m_sampInfos; }
  const std::unordered_map<uint8_t, konami_vendetta_drum_info> drumKeyMap() { return m_drumKeyMap; }

private:
  uint32_t m_instrTableOffsetYM2151;
  uint32_t m_instrTableOffsetK053260;
  uint32_t m_sampInfosOffset;
  uint32_t m_drumBanksOffset;
  uint32_t m_drumsOffset;
  vendetta_sub_offsets m_subOffsets;
  std::vector<konami_tmnt2_ym2151_instr> m_instrsYM2151;
  std::vector<konami_vendetta_instr_k053260> m_instrsK053260;
  std::vector<konami_vendetta_sample_info> m_sampInfos;
  std::unordered_map<uint8_t, konami_vendetta_drum_info> m_drumKeyMap;

  KonamiTMNT2FormatVer m_fmtVer;
};
