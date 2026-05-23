/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMSampColl.h"
#include "VGMInstrSet.h"
#include <array>

// Mystic Warrior driver sample info

enum KonamiArcadeFormatVer : uint8_t;

struct konami_mw_sample_info {
  enum class sample_type: uint8_t {
    PCM_8 = 0,
    PCM_16 = 4,
    ADPCM = 8,
  };

  uint8_t loop_lsb;
  uint8_t loop_mid;
  uint8_t loop_msb;
  uint8_t start_lsb;
  uint8_t start_mid;
  uint8_t start_msb;
  uint8_t flags;
  bool loops;
  uint8_t attenuation;

private:
  static constexpr uint8_t mask_sample_type = 0b0000'1100;  // bits 2-3
  static constexpr uint8_t mask_reverse     = 0b0010'0000;  // bit 5

public:
  [[nodiscard]] constexpr sample_type type() const noexcept {
    return static_cast<sample_type>(flags & mask_sample_type);
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
    uint8_t samp_num;
    uint8_t unity_key;
    int8_t pitch_bend;
    uint8_t pan;
    uint8_t unknown_0;
    uint8_t unknown_1;
    uint8_t default_duration;
    uint8_t attenuation;
  };

  KonamiArcadeInstrSet(RawFile *file,
                       uint32_t offset,
                       std::string name,
                       uint32_t drumTableOffset,
                       uint32_t drumSampleTableOffset,
                       KonamiArcadeFormatVer fmtVer);
  ~KonamiArcadeInstrSet() override = default;

  void addSampleInfoChildren(VGMItem* sampInfoItem, uint32_t off);
  bool parseInstrPointers() override;
  const std::array<drum, 46>& drums() { return m_drums;}

private:
  KonamiArcadeFormatVer m_fmtVer;
  uint32_t m_drumTableOffset;
  uint32_t m_drumSampleTableOffset;
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
    uint32_t offset,
    uint32_t length = 0,
    std::string name = std::string("Konami MW Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  uint32_t determineSampleSize(uint32_t startOffset, konami_mw_sample_info::sample_type type, bool reverse);

  std::vector<VGMItem*> samplePointers;
  KonamiArcadeInstrSet *instrset;
  const std::vector<konami_mw_sample_info> sampInfos;
};
