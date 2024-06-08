#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "PrismSnesFormat.h"

// *****************
// PrismSnesInstrSet
// *****************

class PrismSnesInstrSet:
    public VGMInstrSet {
 public:
  PrismSnesInstrSet(RawFile *file,
                    PrismSnesVersion ver,
                    uint32_t spcDirAddr,
                    uint16_t addrADSR1Table,
                    uint16_t addrADSR2Table,
                    uint16_t addrTuningTableHigh,
                    uint16_t addrTuningTableLow,
                    const std::string &name = "PrismSnesInstrSet");
  virtual ~PrismSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  PrismSnesVersion version;

 protected:
  uint32_t spcDirAddr;
  uint16_t addrADSR1Table;
  uint16_t addrADSR2Table;
  uint16_t addrTuningTableHigh;
  uint16_t addrTuningTableLow;
  std::vector<uint8_t> usedSRCNs;
};

// **************
// PrismSnesInstr
// **************

class PrismSnesInstr
    : public VGMInstr {
 public:
  PrismSnesInstr(VGMInstrSet *instrSet,
                 PrismSnesVersion ver,
                 uint8_t srcn,
                 uint32_t spcDirAddr,
                 uint16_t addrADSR1Entry,
                 uint16_t addrADSR2Entry,
                 uint16_t addrTuningEntryHigh,
                 uint16_t addrTuningEntryLow,
                 const std::string &name = "PrismSnesInstr");
  virtual ~PrismSnesInstr();

  virtual bool loadInstr();

  PrismSnesVersion version;

 protected:
  uint8_t srcn;
  uint32_t spcDirAddr;
  uint16_t addrADSR1Entry;
  uint16_t addrADSR2Entry;
  uint16_t addrTuningEntryHigh;
  uint16_t addrTuningEntryLow;
};

// ************
// PrismSnesRgn
// ************

class PrismSnesRgn
    : public VGMRgn {
 public:
  PrismSnesRgn(PrismSnesInstr *instr,
               PrismSnesVersion ver,
               uint8_t srcn,
               uint32_t spcDirAddr,
               uint16_t addrADSR1Entry,
               uint16_t addrADSR2Entry,
               uint16_t addrTuningEntryHigh,
               uint16_t addrTuningEntryLow);
  virtual ~PrismSnesRgn();

  virtual bool loadRgn();

  PrismSnesVersion version;
};
