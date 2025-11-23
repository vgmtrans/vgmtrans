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
  u8 unknown0;
  u8 unknown1;
  u8 unknown2;

private:
  // static constexpr u8 mask_sample_type = 0b0000'1100;  // bits 2-3
  // static constexpr u8 mask_reverse     = 0b0010'0000;  // bit 5

public:
  // [[nodiscard]] constexpr sample_type start_adds_length() const noexcept {
  //   return static_cast<sample_type>(flags & 8);
  // }

  [[nodiscard]] constexpr bool isAdpcm() const noexcept {
    return (flags & 0x10) > 0;
  }

  [[nodiscard]] constexpr u32 start() const noexcept {
    return (start_msb << 16) + (start_mid << 8) + start_lsb;
  }
  //
  // [[nodiscard]] constexpr bool reverse() const noexcept {
  //   return (flags & mask_reverse) != 0;
  // }
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
  u8 unknown1;
  u8 unknown2;
  u8 unknown3;
  u8 unknown4;

private:
  // static constexpr u8 mask_sample_type = 0b0000'1100;  // bits 2-3
  // static constexpr u8 mask_reverse     = 0b0010'0000;  // bit 5

public:
  // [[nodiscard]] constexpr sample_type start_adds_length() const noexcept {
  //   return static_cast<sample_type>(flags & 8);
  // }

  [[nodiscard]] constexpr bool isAdpcm() const noexcept {
    return flags & 0x80;
  }

  [[nodiscard]] constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }
  // [[nodiscard]] constexpr sample_type type() const noexcept {
  //   return static_cast<sample_type>(flags & mask_sample_type);
  // }
  //
  // [[nodiscard]] constexpr bool reverse() const noexcept {
  //   return (flags & mask_reverse) != 0;
  // }
};


class KonamiTMNT2InstrSet
    : public VGMInstrSet {
public:

  KonamiTMNT2InstrSet(RawFile *file,
                       u32 offset,
                       u32 instrTableAddr,
                       u32 drumTableAddr,
                       const std::vector<konami_tmnt2_instr_info>& instrInfos,
                       const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
                       std::string name,
                       KonamiTMNT2FormatVer fmtVer);
  ~KonamiTMNT2InstrSet() override = default;

  void addInstrInfoChildren(VGMItem* sampInfoItem, u32 off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();
  // const std::array<drum, 46>& drums() { return m_drums;}

private:
  u32 m_instrTableAddr;
  u32 m_drumTableAddr;
  const std::vector<konami_tmnt2_instr_info> m_instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> m_drumTables;

  KonamiTMNT2FormatVer m_fmtVer;
  // u32 m_drumTableOffset;
  // u32 m_drumSampleTableOffset;
  // std::array<drum, 46> m_drums;
};


class KonamiTMNT2SampColl
    : public VGMSampColl {
public:
  KonamiTMNT2SampColl(
    RawFile* file,
    KonamiTMNT2InstrSet* instrset,
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
  KonamiTMNT2InstrSet *instrset;
  const std::vector<konami_tmnt2_instr_info> instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
};
