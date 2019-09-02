#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "AkaoSnesFormat.h"

// ****************
// AkaoSnesInstrSet
// ****************

class AkaoSnesInstrSet:
    public VGMInstrSet {
 public:
  static const uint32_t DRUMKIT_PROGRAM = (0x7F << 14);

  AkaoSnesInstrSet(RawFile *file,
                   AkaoSnesVersion ver,
                   uint32_t spcDirAddr,
                   uint16_t addrTuningTable,
                   uint16_t addrADSRTable,
                   uint16_t addrDrumKitTable,
                   const std::wstring &name = L"AkaoSnesInstrSet");
  virtual ~AkaoSnesInstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

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
                const std::wstring &name = L"AkaoSnesInstr");
  virtual ~AkaoSnesInstr(void);

  virtual bool LoadInstr();

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
                  const std::wstring &name = L"AkaoSnesDrumKit");
  virtual ~AkaoSnesDrumKit(void);

  virtual bool LoadInstr();

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
  virtual ~AkaoSnesRgn(void);

  bool InitializeRegion(uint8_t srcn,
                        uint32_t spcDirAddr,
                        uint16_t addrADSRTable);
  virtual bool LoadRgn();

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
  static const uint8_t KEY_BIAS = 60;

  AkaoSnesDrumKitRgn(AkaoSnesDrumKit *instr,
                     AkaoSnesVersion ver,
                     uint16_t addrTuningTable);
  virtual ~AkaoSnesDrumKitRgn(void);

  bool InitializePercussionRegion(uint8_t srcn,
                                  uint32_t spcDirAddr,
                                  uint16_t addrADSRTable,
                                  uint16_t addrDrumKitTable);

  uint8_t srcn;
};
