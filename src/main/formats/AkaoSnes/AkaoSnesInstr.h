#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "AkaoSnesFormat.h"

// ****************
// AkaoSnesInstrSet
// ****************

class AkaoSnesInstrSet:
    public VGMInstrSet {
 public:
  static constexpr u32 DRUMKIT_PROGRAM = (0x7F << 7);

  AkaoSnesInstrSet(RawFile *file,
                   AkaoSnesVersion ver,
                   u32 spcDirAddr,
                   u16 addrTuningTable,
                   u16 addrADSRTable,
                   u16 addrDrumKitTable,
                   const std::string &name = "AkaoSnesInstrSet");
  ~AkaoSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  AkaoSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrTuningTable;
  u16 addrADSRTable;
  u16 addrDrumKitTable;
  std::vector<u8> usedSRCNs;
};

// *************
// AkaoSnesInstr
// *************

class AkaoSnesInstr
    : public VGMInstr {
 public:
  AkaoSnesInstr(VGMInstrSet *instrSet,
                AkaoSnesVersion ver,
                u8 srcn,
                u32 spcDirAddr,
                u16 addrTuningTable,
                u16 addrADSRTable,
                const std::string &name = "AkaoSnesInstr");
  ~AkaoSnesInstr() override;

  bool loadInstr() override;

  AkaoSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrTuningTable;
  u16 addrADSRTable;
};

// *************
// AkaoSnesDrumKit
// *************

class AkaoSnesDrumKit
  : public VGMInstr {
public:
  AkaoSnesDrumKit(VGMInstrSet *instrSet,
                  AkaoSnesVersion ver,
                  u32 programNum,
                  u32 spcDirAddr,
                  u16 addrTuningTable,
                  u16 addrADSRTable,
                  u16 addrDrumKitTable,
                  const std::string &name = "AkaoSnesDrumKit");
  ~AkaoSnesDrumKit() override;

  bool loadInstr() override;

  AkaoSnesVersion version;

protected:
  u32 spcDirAddr;
  u16 addrTuningTable;
  u16 addrADSRTable;
  u16 addrDrumKitTable;
};

// ***********
// AkaoSnesRgn
// ***********

class AkaoSnesRgn
    : public VGMRgn {
 public:
  AkaoSnesRgn(VGMInstr *instr,
              AkaoSnesVersion ver,
              u16 addrTuningTable);
  ~AkaoSnesRgn() override;

  bool initializeRegion(u8 srcn,
                        u32 spcDirAddr,
                        u16 addrADSRTable);
  bool loadRgn() override;

  AkaoSnesVersion version;
};

// ***********
// AkaoSnesDrumKitRgn
// ***********

class AkaoSnesDrumKitRgn
  : public AkaoSnesRgn {
public:
  // We need some space to move for unityKey, since we might need it to go
  // less than the current note, so we add this to all the percussion notes.
  // This value can be anything from 0 to 127, but being near the middle of
  // the range gives it a decent chance of not going out of range in either
  // direction.
  static constexpr u8 KEY_BIAS = 60;

  AkaoSnesDrumKitRgn(AkaoSnesDrumKit *instr,
                     AkaoSnesVersion ver,
                     u16 addrTuningTable);
  ~AkaoSnesDrumKitRgn() override;

  bool initializePercussionRegion(u8 srcn,
                                  u32 spcDirAddr,
                                  u16 addrADSRTable,
                                  u16 addrDrumKitTable);
};
