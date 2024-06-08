#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

// **********
// WDInstrSet
// **********

class WDInstrSet : public VGMInstrSet {
 public:
  WDInstrSet(RawFile *file, uint32_t offset);
  ~WDInstrSet() override = default;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  uint32_t dwSampSectSize{};
  uint32_t dwNumInstrs{};
  uint32_t dwTotalRegions{};
};

// *******
// WDInstr
// *******

class WDInstr : public VGMInstr {
 public:
  WDInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
          uint32_t theInstrNum, const std::string& name);
  ~WDInstr() override = default;
  bool loadInstr() override;
};

// *****
// WDRgn
// *****

class WDRgn : public VGMRgn {
 public:
  WDRgn(WDInstr *instr, uint32_t offset);
  ~WDRgn() override = default;

  uint16_t ADSR1{};  // raw ps2 ADSR1 value (articulation data)
  uint16_t ADSR2{};  // raw ps2 ADSR2 value (articulation data)
  uint8_t bStereoRegion{};
  uint8_t bFirstRegion{};
  uint8_t bLastRegion{};
  uint8_t bUnknownFlag2{};
};
