/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"

#include "VGMSamp.h"

enum class KonamiAdpcmChip {
  K054539,
  K053260
};

class KonamiAdpcmSamp : public VGMSamp {
public:
  KonamiAdpcmSamp(
      VGMSampColl* sampColl,
      u32 offset,
      u32 length,
      KonamiAdpcmChip chip,
      u32 theRate,
      std::string name
  );

  double compressionRatio() const override;

private:
  std::vector<u8> decodeToNativePcm() override;
  KonamiAdpcmChip m_chip;
  const s16* m_stepTable;
};
