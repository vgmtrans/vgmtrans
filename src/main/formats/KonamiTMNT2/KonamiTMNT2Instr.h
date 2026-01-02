#pragma once
#include "VGMSampColl.h"
#include "VGMInstrSet.h"

enum KonamiTMNT2FormatVer : uint8_t;
class RawFile;
struct konami_tmnt2_instr_info {
  u8 flags;
  u8 length_lsb;
  u8 length_msb;
  u8 start_lsb;
  u8 start_mid;
  u8 start_msb;
  u8 volume;
  u8 note_dur;
  u8 release_dur_and_rate;
  u8 default_pan;

private:
  static constexpr u8 mask_adpcm   = 0b0001'0000;  // bits 2-3
  static constexpr u8 mask_reverse = 0b0000'1000;  // bit 5

public:
  [[nodiscard]] constexpr bool isAdpcm() const noexcept {
    return (flags & mask_adpcm) > 0;
  }

  [[nodiscard]] constexpr bool reverse() const noexcept {
    return (flags & mask_reverse) != 0;
  }

  [[nodiscard]] constexpr u32 start() const noexcept {
    return (start_msb << 16) + (start_mid << 8) + start_lsb;
  }
};

struct konami_tmnt2_drum_info {
  u8 pitch_lo;
  u8 pitch_hi;
  u8 unknown0;
  u8 flags;
  u8 length_lo;
  u8 length_hi;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 volume;
  u8 note_dur_lo;
  u8 note_dur_hi;
  u8 release_dur_and_rate;
  u8 default_pan;

public:
  [[nodiscard]] constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }
};


class KonamiTMNT2SampleInstrSet
    : public VGMInstrSet {
public:

  KonamiTMNT2SampleInstrSet(RawFile *file,
                       u32 offset,
                       u32 instrTableAddr,
                       u32 drumTableAddr,
                       const std::vector<konami_tmnt2_instr_info>& instrInfos,
                       const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
                       std::string name,
                       KonamiTMNT2FormatVer fmtVer);
  ~KonamiTMNT2SampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, u32 off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  const std::vector<konami_tmnt2_instr_info> instrInfos() { return m_instrInfos; }
  const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables() { return m_drumTables; }

private:
  u32 m_instrTableAddr;
  u32 m_drumTableAddr;
  const std::vector<konami_tmnt2_instr_info> m_instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> m_drumTables;

  KonamiTMNT2FormatVer m_fmtVer;
};

class KonamiTMNT2SampColl
    : public VGMSampColl {
public:
  KonamiTMNT2SampColl(
    RawFile* file,
    KonamiTMNT2SampleInstrSet* instrset,
    const std::vector<konami_tmnt2_instr_info>& instrInfos,
    const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
    u32 offset,
    u32 length = 0,
    std::string name = std::string("Konami TMNT2 Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  KonamiTMNT2SampleInstrSet *instrset;
  const std::vector<konami_tmnt2_instr_info> instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
};
