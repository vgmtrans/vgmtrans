#pragma once

#include "Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "NamcoSnesFormat.h"

// *****************
// NamcoSnesInstrSet
// *****************

class NamcoSnesInstrSet:
    public VGMInstrSet {
 public:
  NamcoSnesInstrSet(RawFile *file,
                    NamcoSnesVersion ver,
                    u32 spcDirAddr,
                    u16 addrTuningTable,
                    const std::string &name = "NamcoSnesInstrSet");
  virtual ~NamcoSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  NamcoSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrTuningTable;
  std::vector<u8> usedSRCNs;
};

// **************
// NamcoSnesInstr
// **************

class NamcoSnesInstr
    : public VGMInstr {
 public:
  NamcoSnesInstr(VGMInstrSet *instrSet,
                 NamcoSnesVersion ver,
                 u8 srcn,
                 u32 spcDirAddr,
                 u16 addrTuningEntry,
                 const std::string &name = "NamcoSnesInstr");
  virtual ~NamcoSnesInstr();

  virtual bool loadInstr();

  NamcoSnesVersion version;

 protected:
  u32 spcDirAddr;
  u16 addrTuningEntry;
};

// ************
// NamcoSnesRgn
// ************

class NamcoSnesRgn
    : public VGMRgn {
 public:
  NamcoSnesRgn
      (NamcoSnesInstr *instr, NamcoSnesVersion ver, u8 srcn, u32 spcDirAddr, u16 addrTuningEntry);
  virtual ~NamcoSnesRgn();

  virtual bool loadRgn();

  NamcoSnesVersion version;
};
