/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
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
                      const std::wstring &name = L"CompileSnesInstrSet");
  virtual ~CompileSnesInstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

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
                   const std::wstring &name = L"CompileSnesInstr");
  virtual ~CompileSnesInstr(void);

  virtual bool LoadInstr();

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
  virtual ~CompileSnesRgn(void);

  virtual bool LoadRgn();

  CompileSnesVersion version;
};
