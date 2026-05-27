#pragma once

#include "Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "CompileSnesFormat.h"

// *******************
// CompileSnesInstrSet
// *******************

class CompileSnesInstrSet:
    public VGMInstrSet {
 public:
  CompileSnesInstrSet(RawFile *file,
                      CompileSnesVersion ver,
                      u16 addrTuningTable,
                      u16 addrPitchTablePtrs,
                      u32 spcDirAddr,
                      const std::string &name = "CompileSnesInstrSet");
  ~CompileSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  CompileSnesVersion version;

 protected:
  u16 addrTuningTable;
  u16 addrPitchTablePtrs;
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;
};

// ****************
// CompileSnesInstr
// ****************

class CompileSnesInstr
    : public VGMInstr {
 public:
  CompileSnesInstr(VGMInstrSet *instrSet,
                   CompileSnesVersion ver,
                   u16 addrTuningTableItem,
                   u16 addrPitchTablePtrs,
                   u8 srcn,
                   u32 spcDirAddr,
                   const std::string &name = "CompileSnesInstr");
  ~CompileSnesInstr() override;

  bool loadInstr() override;

  static u32 expectedSize(CompileSnesVersion version);

  CompileSnesVersion version;

 protected:
  u16 addrPitchTablePtrs;
  u32 spcDirAddr;
};

// **************
// CompileSnesRgn
// **************

class CompileSnesRgn
    : public VGMRgn {
 public:
  CompileSnesRgn(CompileSnesInstr *instr,
                 CompileSnesVersion ver,
                 u16 addrTuningTableItem,
                 u16 addrPitchTablePtrs);
  ~CompileSnesRgn() override;

  bool loadRgn() override;

  CompileSnesVersion version;
};
