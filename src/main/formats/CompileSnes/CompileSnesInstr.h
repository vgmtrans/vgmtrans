#pragma once
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
                      uint16_t addrTuningTable,
                      uint16_t addrPitchTablePtrs,
                      uint32_t spcDirAddr,
                      const std::string &name = "CompileSnesInstrSet");
  ~CompileSnesInstrSet() override;

  bool GetHeaderInfo() override;
  bool GetInstrPointers() override;

  CompileSnesVersion version;

 protected:
  uint16_t addrTuningTable;
  uint16_t addrPitchTablePtrs;
  uint32_t spcDirAddr;
  std::vector<uint8_t> usedSRCNs;
};

// ****************
// CompileSnesInstr
// ****************

class CompileSnesInstr
    : public VGMInstr {
 public:
  CompileSnesInstr(VGMInstrSet *instrSet,
                   CompileSnesVersion ver,
                   uint16_t addrTuningTableItem,
                   uint16_t addrPitchTablePtrs,
                   uint8_t srcn,
                   uint32_t spcDirAddr,
                   const std::string &name = "CompileSnesInstr");
  ~CompileSnesInstr() override;

  bool LoadInstr() override;

  static uint32_t ExpectedSize(CompileSnesVersion version);

  CompileSnesVersion version;

 protected:
  uint16_t addrPitchTablePtrs;
  uint32_t spcDirAddr;
};

// **************
// CompileSnesRgn
// **************

class CompileSnesRgn
    : public VGMRgn {
 public:
  CompileSnesRgn(CompileSnesInstr *instr,
                 CompileSnesVersion ver,
                 uint16_t addrTuningTableItem,
                 uint16_t addrPitchTablePtrs,
                 uint8_t srcn,
                 uint32_t spcDirAddr);
  ~CompileSnesRgn() override;

  bool LoadRgn() override;

  CompileSnesVersion version;
};
