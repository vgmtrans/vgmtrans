#pragma once

#include "base/Types.h"
#include "NinSnesProfile.h"

#include <string>
#include <vector>

struct NinSnesScanResult {
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profile = NinSnesProfileId::Unknown;

  std::string name;

  u8 songIndex = 0xff;
  u32 songListAddr = 0;
  u32 songStartAddr = 0;
  u8 sectionPtrAddr = 0;

  u32 instrTableAddr = 0;
  u16 spcDirAddr = 0;

  u16 konamiBaseAddress = 0;
  u16 falcomBaseOffset = 0;
  u8 quintetBGMInstrBase = 0;
  u16 quintetAddrBGMInstrLookup = 0;
  u16 konamiTuningTableAddress = 0;
  u8 konamiTuningTableSize = 0;

  std::vector<u8> volumeTable;
  std::vector<u8> durRateTable;

  [[nodiscard]] const NinSnesProfile& profileDefinition() const {
    return getNinSnesProfile(profile);
  }
};
