#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "SuzukiSnesFormat.h"

// ******************
// SuzukiSnesInstrSet
// ******************

class SuzukiSnesInstrSet:
    public VGMInstrSet {
 public:
  static constexpr uint32_t DRUMKIT_PROGRAM = (0x7F << 7);

  SuzukiSnesInstrSet(RawFile *file,
                     SuzukiSnesVersion ver,
                     uint32_t spcDirAddr,
                     uint16_t addrSRCNTable,
                     uint16_t addrVolumeTable,
                     uint16_t addrADSRTable,
                     uint16_t addrTuningTable,
                     uint16_t addrDrumKitTable,
                     const std::string &name = "SuzukiSnesInstrSet");
  virtual ~SuzukiSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  SuzukiSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrSRCNTable;
  uint16_t addrVolumeTable;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
  uint16_t addrDrumKitTable;
  std::vector<uint8_t> usedSRCNs;
};

// *************
// SuzukiSnesInstr
// *************

class SuzukiSnesInstr
    : public VGMInstr {
 public:
  SuzukiSnesInstr(VGMInstrSet *instrSet,
                  SuzukiSnesVersion ver,
                  uint8_t instrNum,
                  uint32_t spcDirAddr,
                  uint16_t addrSRCNTable,
                  uint16_t addrVolumeTable,
                  uint16_t addrADSRTable,
                  uint16_t addrTuningTable,
                  const std::string &name = "SuzukiSnesInstr");
  virtual ~SuzukiSnesInstr();

  virtual bool loadInstr();

  SuzukiSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrSRCNTable;
  uint16_t addrVolumeTable;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
};

// *****************
// SuzukiSnesDrumKit
// *****************

class SuzukiSnesDrumKit
  : public VGMInstr {
public:
  SuzukiSnesDrumKit(VGMInstrSet *instrSet,
                    SuzukiSnesVersion ver,
                    uint32_t programNum,
                    uint32_t spcDirAddr,
                    uint16_t addrSRCNTable,
                    uint16_t addrTuningTable,
                    uint16_t addrADSRTable,
                    uint16_t addrDrumKitTable,
                  const std::string &name = "SuzukiSnesDrumKit");
  ~SuzukiSnesDrumKit() override;

  bool loadInstr() override;

  SuzukiSnesVersion version;

protected:
  uint32_t spcDirAddr;
  uint16_t addrSRCNTable;
  uint16_t addrTuningTable;
  uint16_t addrADSRTable;
  uint16_t addrDrumKitTable;
};

// *************
// SuzukiSnesRgn
// *************

class SuzukiSnesRgn
    : public VGMRgn {
 public:
  SuzukiSnesRgn(VGMInstr *instr,
                SuzukiSnesVersion ver,
                uint16_t addrSRCNTable);
   virtual ~SuzukiSnesRgn();

  bool initializeCommonRegion(uint8_t srcn,
                              uint32_t spcDirAddr,
                              uint16_t addrADSRTable,
                              uint16_t addrTuningTable);
  bool initializeNonPercussionRegion(uint8_t instrNum, uint16_t addrVolumeTable);
  virtual bool loadRgn();

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
  static constexpr uint8_t KEY_BIAS = 60;

  SuzukiSnesDrumKitRgn(SuzukiSnesDrumKit *instr,
                       SuzukiSnesVersion ver,
                       uint16_t addrDrumKitTable);
  ~SuzukiSnesDrumKitRgn() override;

  bool initializePercussionRegion(uint16_t noteOffset,
                                  uint32_t spcDirAddr,
                                  uint16_t addrSRCNTable,
                                  uint16_t addrADSRTable,
                                  uint16_t addrTuningTable);
};