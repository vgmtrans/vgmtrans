#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "NinSnesFormat.h"

// ****************
// NinSnesInstrSet
// ****************

class NinSnesInstrSet:
    public VGMInstrSet {
 public:
  NinSnesInstrSet(RawFile *file,
                  NinSnesVersion ver,
                  uint32_t offset,
                  uint32_t spcDirAddr,
                  const std::string &name = "NinSnesInstrSet");
  virtual ~NinSnesInstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

  NinSnesVersion version;

  uint16_t konamiTuningTableAddress;
  uint8_t konamiTuningTableSize;

 protected:
  uint32_t spcDirAddr;
  std::vector<uint8_t> usedSRCNs;
};

// *************
// NinSnesInstr
// *************

class NinSnesInstr
    : public VGMInstr {
 public:
  NinSnesInstr(VGMInstrSet *instrSet,
               NinSnesVersion ver,
               uint32_t offset,
               uint32_t theBank,
               uint32_t theInstrNum,
               uint32_t spcDirAddr,
               const std::string &name = "NinSnesInstr");
  virtual ~NinSnesInstr(void);

  virtual bool LoadInstr();

  static bool IsValidHeader
      (RawFile *file, NinSnesVersion version, uint32_t addrInstrHeader, uint32_t spcDirAddr, bool validateSample);
  static uint32_t ExpectedSize(NinSnesVersion version);

  NinSnesVersion version;

  uint16_t konamiTuningTableAddress;
  uint8_t konamiTuningTableSize;

 protected:
  uint32_t spcDirAddr;
};

// ***********
// NinSnesRgn
// ***********

class NinSnesRgn
    : public VGMRgn {
 public:
  NinSnesRgn(NinSnesInstr *instr,
             NinSnesVersion ver,
             uint32_t offset,
             uint16_t konamiTuningTableAddress = 0,
             uint8_t konamiTuningTableSize = 0);
  virtual ~NinSnesRgn(void);

  virtual bool LoadRgn();

  NinSnesVersion version;
};
