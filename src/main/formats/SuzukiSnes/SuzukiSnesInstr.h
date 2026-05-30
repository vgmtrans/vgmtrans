#pragma once

#include "base/Types.h"
#include "SuzukiSnesFormat.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <string>

// ******************
// SuzukiSnesInstrSet
// ******************

class SuzukiSnesInstrSet:
    public VGMInstrSet {
 public:
  static constexpr u32 DRUMKIT_PROGRAM = (0x7F << 7);

  SuzukiSnesInstrSet(RawFile *file,
                     SuzukiSnesVersion ver,
                     u32 spcDirAddr,
                     u16 addrSRCNTable,
                     u16 addrVolumeTable,
                     u16 addrADSRTable,
                     u16 addrTuningTable,
                     const std::string &name = "SuzukiSnesInstrSet");
  ~SuzukiSnesInstrSet() override = default;

  bool parseHeader() override;
  bool parseInstrPointers() override;
  void useColl(const VGMColl* coll) override;

  SuzukiSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrSRCNTable;
  u16 addrVolumeTable;
  u16 addrTuningTable;
  u16 addrADSRTable;
};

// *************
// SuzukiSnesInstr
// *************

class SuzukiSnesInstr
    : public VGMInstr {
 public:
  SuzukiSnesInstr(VGMInstrSet *instrSet,
                  SuzukiSnesVersion ver,
                  u8 instrNum,
                  u32 spcDirAddr,
                  u16 addrSRCNTable,
                  u16 addrVolumeTable,
                  u16 addrADSRTable,
                  u16 addrTuningTable,
                  const std::string &name = "SuzukiSnesInstr");
  ~SuzukiSnesInstr() override;

  bool loadInstr() override;

  SuzukiSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrSRCNTable;
  u16 addrVolumeTable;
  u16 addrTuningTable;
  u16 addrADSRTable;
};

// *****************
// SuzukiSnesDrumKit
// *****************

class SuzukiSnesDrumKit
  : public VGMInstr {
public:
  SuzukiSnesDrumKit(VGMInstrSet *instrSet,
                    SuzukiSnesVersion ver,
                    u32 programNum,
                    u32 spcDirAddr,
                    u16 addrSRCNTable,
                    u16 addrTuningTable,
                    u16 addrADSRTable,
                    u16 addrDrumKitTable,
                  const std::string &name = "SuzukiSnesDrumKit");
  ~SuzukiSnesDrumKit() override = default;

  bool loadInstr() override;

  SuzukiSnesVersion version;

protected:
  u32 spcDirAddr;
  u16 addrSRCNTable;
  u16 addrTuningTable;
  u16 addrADSRTable;
  u16 addrDrumKitTable;
};

// *************
// SuzukiSnesRgn
// *************

class SuzukiSnesRgn
    : public VGMRgn {
 public:
  SuzukiSnesRgn(VGMInstr *instr,
                SuzukiSnesVersion ver,
                u16 addrSRCNTable);
  ~SuzukiSnesRgn() override;

  bool initializeCommonRegion(u8 srcn,
                              u32 spcDirAddr,
                              u16 addrADSRTable,
                              u16 addrTuningTable);
  bool initializeNonPercussionRegion(u8 instrNum, u16 addrVolumeTable);
  bool loadRgn() override;

  SuzukiSnesVersion version;
};

// ********************
// SuzukiSnesDrumKitRgn
// ********************

class SuzukiSnesDrumKitRgn
  : public SuzukiSnesRgn {
public:
  // We need some space to move for unityKey, since we might need it to go
  // less than the current note, so we add this to all the percussion notes.
  // This value can be anything from 0 to 127, but being near the middle of
  // the range gives it a decent chance of not going out of range in either
  // direction.
  static constexpr u8 KEY_BIAS = 60;

  SuzukiSnesDrumKitRgn(SuzukiSnesDrumKit *instr,
                       SuzukiSnesVersion ver,
                       u16 addrDrumKitTable);
  ~SuzukiSnesDrumKitRgn() override;

  bool initializePercussionRegion(u16 noteOffset,
                                  u32 spcDirAddr,
                                  u16 addrSRCNTable,
                                  u16 addrADSRTable,
                                  u16 addrTuningTable);
};
