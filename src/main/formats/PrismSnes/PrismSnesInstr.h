#pragma once

#include "Types.h"
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
                    u32 spcDirAddr,
                    u16 addrADSR1Table,
                    u16 addrADSR2Table,
                    u16 addrTuningTableHigh,
                    u16 addrTuningTableLow,
                    const std::string &name = "PrismSnesInstrSet");
  virtual ~PrismSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  PrismSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrADSR1Table;
  u16 addrADSR2Table;
  u16 addrTuningTableHigh;
  u16 addrTuningTableLow;
  std::vector<u8> usedSRCNs;
};

// **************
// PrismSnesInstr
// **************

class PrismSnesInstr
    : public VGMInstr {
 public:
  PrismSnesInstr(VGMInstrSet *instrSet,
                 PrismSnesVersion ver,
                 u8 srcn,
                 u32 spcDirAddr,
                 u16 addrADSR1Entry,
                 u16 addrADSR2Entry,
                 u16 addrTuningEntryHigh,
                 u16 addrTuningEntryLow,
                 const std::string &name = "PrismSnesInstr");
  virtual ~PrismSnesInstr();

  virtual bool loadInstr();

  PrismSnesVersion version;

 protected:
  u8 srcn;
  u32 spcDirAddr;
  u16 addrADSR1Entry;
  u16 addrADSR2Entry;
  u16 addrTuningEntryHigh;
  u16 addrTuningEntryLow;
};

// ************
// PrismSnesRgn
// ************

class PrismSnesRgn
    : public VGMRgn {
 public:
  PrismSnesRgn(PrismSnesInstr *instr,
               PrismSnesVersion ver,
               u8 srcn,
               u32 spcDirAddr,
               u16 addrADSR1Entry,
               u16 addrADSR2Entry,
               u16 addrTuningEntryHigh,
               u16 addrTuningEntryLow);
  virtual ~PrismSnesRgn();

  virtual bool loadRgn();

  PrismSnesVersion version;
};
