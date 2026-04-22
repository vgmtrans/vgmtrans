#pragma once

#include <string>
#include <vector>

#include "NinSnesProfile.h"

struct NinSnesScanResult {
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profile = NinSnesProfileId::Unknown;

  std::string name;

  uint8_t songIndex = 0xff;
  uint32_t songListAddr = 0;
  uint32_t songStartAddr = 0;
  uint8_t sectionPtrAddr = 0;

  uint32_t instrTableAddr = 0;
  uint16_t spcDirAddr = 0;

  uint16_t konamiBaseAddress = 0;
  uint16_t falcomBaseOffset = 0;
  uint8_t quintetBGMInstrBase = 0;
  uint16_t quintetAddrBGMInstrLookup = 0;
  uint16_t konamiTuningTableAddress = 0;
  uint8_t konamiTuningTableSize = 0;

  std::vector<uint8_t> volumeTable;
  std::vector<uint8_t> durRateTable;

  [[nodiscard]] const NinSnesProfile& profileDefinition() const {
    return getNinSnesProfile(profile);
  }
};
