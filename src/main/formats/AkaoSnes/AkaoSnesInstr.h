#pragma once
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "AkaoSnesFormat.h"

// ****************
// AkaoSnesInstrSet
// ****************

class AkaoSnesInstrSet:
    public VGMInstrSet {
 public:
  static constexpr uint32_t DRUMKIT_PROGRAM = (0x7F << 7);

  AkaoSnesInstrSet(RawFile *file,
                   AkaoSnesVersion ver,
                   uint32_t spcDirAddr,
                   uint16_t addrTuningTable,
                   uint16_t addrADSRTable,
                   uint16_t addrDrumKitTable,
                   const std::string &name = "AkaoSnesInstrSet");
  ~AkaoSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  AkaoSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
  uint16_t addrDrumKitTable;
  std::vector<uint8_t> usedSRCNs;
};

// *************
// AkaoSnesInstr
// *************

class AkaoSnesInstr
    : public VGMInstr {
 public:
  AkaoSnesInstr(VGMInstrSet *instrSet,
                AkaoSnesVersion ver,
                uint8_t srcn,
                uint32_t spcDirAddr,
                uint16_t addrTuningTable,
                uint16_t addrADSRTable,
                const std::string &name = "AkaoSnesInstr");
  ~AkaoSnesInstr() override;

  bool loadInstr() override;

  AkaoSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
};

// *************
// AkaoSnesDrumKit
// *************

class AkaoSnesDrumKit
  : public VGMInstr {
public:
  AkaoSnesDrumKit(VGMInstrSet *instrSet,
                  AkaoSnesVersion ver,
                  uint32_t programNum,
                  uint32_t spcDirAddr,
                  uint16_t addrTuningTable,
                  uint16_t addrADSRTable,
                  uint16_t addrDrumKitTable,
                  const std::string &name = "AkaoSnesDrumKit");
  ~AkaoSnesDrumKit() override;

  bool loadInstr() override;

  AkaoSnesVersion version;

protected:
  uint32_t spcDirAddr;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
  uint16_t addrDrumKitTable;
};

// ***********
// AkaoSnesRgn
// ***********

class AkaoSnesRgn
    : public VGMRgn {
 public:
  AkaoSnesRgn(VGMInstr *instr,
              AkaoSnesVersion ver,
              uint16_t addrTuningTable);
  ~AkaoSnesRgn() override;

  bool initializeRegion(uint8_t srcn,
                        uint32_t spcDirAddr,
                        uint16_t addrADSRTable);
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
  static constexpr uint8_t KEY_BIAS = 60;

  AkaoSnesDrumKitRgn(AkaoSnesDrumKit *instr,
                     AkaoSnesVersion ver,
                     uint16_t addrTuningTable);
  ~AkaoSnesDrumKitRgn() override;

  bool initializePercussionRegion(uint8_t srcn,
                                  uint32_t spcDirAddr,
                                  uint16_t addrADSRTable,
                                  uint16_t addrDrumKitTable);
};
