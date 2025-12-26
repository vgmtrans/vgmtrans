/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "K054539.h"
#include "VGMSampColl.h"
#include "VGMInstrSet.h"
#include <array>

// Mystic Warrior driver sample info

enum KonamiArcadeFormatVer : uint8_t;

struct konami_mw_sample_info {
  u8 loop_lsb;
  u8 loop_mid;
  u8 loop_msb;
  u8 start_lsb;
  u8 start_mid;
  u8 start_msb;
  u8 flags;
  bool loops;
  u8 attenuation;

private:
  static constexpr u8 mask_sample_type = 0b0000'1100;  // bits 2-3
  static constexpr u8 mask_reverse     = 0b0010'0000;  // bit 5

public:
  [[nodiscard]] constexpr k054539_sample_type type() const noexcept {
    return static_cast<k054539_sample_type>(flags & mask_sample_type);
  }

  [[nodiscard]] constexpr bool reverse() const noexcept {
    return (flags & mask_reverse) != 0;
  }
};

// ********************
// KonamiArcadeInstrSet
// ********************

class KonamiArcadeInstrSet
    : public VGMInstrSet {
public:
  struct drum {
    u8 samp_num;
    u8 unity_key;
    s8 pitch_bend;
    u8 pan;
    u8 unknown_0;
    u8 unknown_1;
    u8 default_duration;
    u8 attenuation;
  };

  KonamiArcadeInstrSet(RawFile *file,
                       u32 offset,
                       std::string name,
                       u32 drumTableOffset,
                       u32 drumSampleTableOffset,
                       KonamiArcadeFormatVer fmtVer);
  ~KonamiArcadeInstrSet() override = default;

  void addSampleInfoChildren(VGMItem* sampInfoItem, u32 off);
  bool parseInstrPointers() override;
  const std::array<drum, 46>& drums() { return m_drums;}

private:
  KonamiArcadeFormatVer m_fmtVer;
  u32 m_drumTableOffset;
  u32 m_drumSampleTableOffset;
  std::array<drum, 46> m_drums;
};

// ********************
// KonamiArcadeSampColl
// ********************

class KonamiArcadeSampColl
    : public VGMSampColl {
public:
  KonamiArcadeSampColl(
    RawFile* file,
    KonamiArcadeInstrSet* instrset,
    const std::vector<konami_mw_sample_info>& sampInfos,
    u32 offset,
    u32 length = 0,
    std::string name = std::string("Konami MW Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  KonamiArcadeInstrSet *instrset;
  const std::vector<konami_mw_sample_info> sampInfos;
};
