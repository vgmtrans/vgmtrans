/*
* VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "common.h"

class RawFile;
class VGMItem;
enum KonamiTMNT2FormatVer : uint8_t;

struct konami_vendetta_instr {
  u8 samp_info_idx;
  u8 unknown;
  u8 attenuation;
};

struct konami_vendetta_sample_info {
  u8 pitch_lo;
  u8 pitch_hi;
  u8 length_lo;
  u8 length_hi;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 volume;
};


class KonamiVendettaSampleInstrSet
    : public VGMInstrSet {
public:

  KonamiVendettaSampleInstrSet(RawFile *file,
                               u32 offset,
                               u32 instrTableAddr,
                               u32 drumTableAddr,
                               const std::vector<konami_vendetta_instr_info>& instrInfos,
                               const std::vector<std::vector<konami_vendeetta_drum_info>>& drumBanks,
                               std::string name,
                               KonamiTMNT2FormatVer fmtVer);
  ~KonamiVendettaSampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, u32 off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  const std::vector<konami_vendetta_instr_info> instrInfos() { return m_instrInfos; }
  const std::vector<konami_vendetta_sample_info> sampleInfos() { return m_sampleInfos; }
  const std::vector<std::vector<konami_vendeetta_drum_info>> drumBanks() { return m_drumBanks; }

private:
  u32 m_instrTableAddr;
  u32 m_drumTableAddr;
  const std::vector<konami_vendetta_instr_info> m_instrInfos;
  const std::vector<konami_vendetta_sample_info> m_sampleInfos;
  const std::vector<std::vector<konami_vendeetta_drum_info>> m_drumBanks;

  KonamiTMNT2FormatVer m_fmtVer;
};
//
// class KonamiTMNT2SampColl
//     : public VGMSampColl {
// public:
//   KonamiTMNT2SampColl(
//     RawFile* file,
//     KonamiTMNT2SampleInstrSet* instrset,
//     const std::vector<konami_tmnt2_instr_info>& instrInfos,
//     const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
//     u32 offset,
//     u32 length = 0,
//     std::string name = std::string("Konami TMNT2 Sample Collection")
//   );
//   bool parseHeader() override;
//   bool parseSampleInfo() override;
//
// private:
//   std::vector<VGMItem*> samplePointers;
//   KonamiTMNT2SampleInstrSet *instrset;
//   const std::vector<konami_tmnt2_instr_info> instrInfos;
//   const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
// };
