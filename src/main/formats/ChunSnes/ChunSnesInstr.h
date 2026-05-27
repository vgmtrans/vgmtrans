#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "ChunSnesFormat.h"

// ****************
// ChunSnesInstrSet
// ****************

class ChunSnesInstrSet:
    public VGMInstrSet {
 public:
  ChunSnesInstrSet(RawFile *file,
                   ChunSnesVersion ver,
                   u16 addrInstrSetTable,
                   u16 addrSampNumTable,
                   u16 addrSampleTable,
                   u32 spcDirAddr,
                   const std::string &name = "ChunSnesInstrSet");
  ~ChunSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  ChunSnesVersion version;

 protected:
  u16 addrSampNumTable;
  u16 addrSampleTable;
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;
};

// *************
// ChunSnesInstr
// *************

class ChunSnesInstr
    : public VGMInstr {
 public:
  ChunSnesInstr(VGMInstrSet *instrSet,
                ChunSnesVersion ver,
                u8 theInstrNum,
                u16 addrInstr,
                u16 addrSampleTable,
                u32 spcDirAddr,
                const std::string &name = "ChunSnesInstr");
  ~ChunSnesInstr() override;

  bool loadInstr() override;

  ChunSnesVersion version;

 protected:
  u16 addrSampleTable;
  u32 spcDirAddr;
};

// ***********
// ChunSnesRgn
// ***********

class ChunSnesRgn
    : public VGMRgn {
 public:
  ChunSnesRgn(ChunSnesInstr *instr, ChunSnesVersion ver, u8 srcn, u16 addrRgn, u32 spcDirAddr);
  ~ChunSnesRgn() override;

  bool loadRgn() override;

  ChunSnesVersion version;
};
