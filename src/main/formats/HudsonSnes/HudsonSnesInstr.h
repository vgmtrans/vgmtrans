#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "HudsonSnesFormat.h"

// ******************
// HudsonSnesInstrSet
// ******************

class HudsonSnesInstrSet:
    public VGMInstrSet {
 public:
  HudsonSnesInstrSet(RawFile *file,
                     HudsonSnesVersion ver,
                     u32 offset,
                     u32 length,
                     u32 spcDirAddr,
                     u32 addrSampTuningTable,
                     const std::string &name = "HudsonSnesInstrSet");
  virtual ~HudsonSnesInstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

  HudsonSnesVersion version;

 protected:
  u32 spcDirAddr;
  u32 addrSampTuningTable;
  std::vector<u8> usedSRCNs;
};

// ***************
// HudsonSnesInstr
// ***************

class HudsonSnesInstr
    : public VGMInstr {
 public:
  HudsonSnesInstr(VGMInstrSet *instrSet,
                  HudsonSnesVersion ver,
                  u32 offset,
                  u8 instrNum,
                  u32 spcDirAddr,
                  u32 addrSampTuningTable,
                  const std::string &name = "HudsonSnesInstr");
  virtual ~HudsonSnesInstr();

  virtual bool loadInstr();

  HudsonSnesVersion version;

 protected:
  u32 spcDirAddr;
  u32 addrSampTuningTable;
};

// *************
// HudsonSnesRgn
// *************

class HudsonSnesRgn
    : public VGMRgn {
 public:
  HudsonSnesRgn(HudsonSnesInstr *instr, HudsonSnesVersion ver, u32 offset, u32 addrTuningEntry);
  virtual ~HudsonSnesRgn();

  virtual bool loadRgn();

  HudsonSnesVersion version;
};
