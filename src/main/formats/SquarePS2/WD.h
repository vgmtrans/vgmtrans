#pragma once

#include "util/types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

// **********
// WDInstrSet
// **********

class WDInstrSet : public VGMInstrSet {
 public:
  WDInstrSet(RawFile *file, u32 offset);
  ~WDInstrSet() override = default;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  u32 dwSampSectSize{};
  u32 dwNumInstrs{};
  u32 dwTotalRegions{};
};

// *******
// WDInstr
// *******

class WDInstr : public VGMInstr {
 public:
  WDInstr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 theBank,
          u32 theInstrNum, const std::string& name);
  ~WDInstr() override = default;
  bool loadInstr() override;
};

// *****
// WDRgn
// *****

class WDRgn : public VGMRgn {
 public:
  WDRgn(WDInstr *instr, u32 offset);
  ~WDRgn() override = default;

  u16 ADSR1{};  // raw ps2 ADSR1 value (articulation data)
  u16 ADSR2{};  // raw ps2 ADSR2 value (articulation data)
  u8 bStereoRegion{};
  u8 bFirstRegion{};
  u8 bLastRegion{};
  u8 bUnknownFlag2{};
};
