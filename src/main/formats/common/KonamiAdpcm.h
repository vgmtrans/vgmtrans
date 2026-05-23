/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMSamp.h"

enum class KonamiAdpcmChip {
  K054539,
  K053260
};

class KonamiAdpcmSamp : public VGMSamp {
public:
  KonamiAdpcmSamp(
      VGMSampColl* sampColl,
      uint32_t offset,
      uint32_t length,
      KonamiAdpcmChip chip,
      uint32_t theRate,
      std::string name
  );

  double compressionRatio() const override;

private:
  std::vector<uint8_t> decodeToNativePcm() override;
  KonamiAdpcmChip m_chip;
  const int16_t* m_stepTable;
};
