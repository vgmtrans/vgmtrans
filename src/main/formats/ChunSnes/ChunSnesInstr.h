#pragma once
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
                   uint16_t addrInstrSetTable,
                   uint16_t addrSampNumTable,
                   uint16_t addrSampleTable,
                   uint32_t spcDirAddr,
                   const std::string &name = "ChunSnesInstrSet");
  ~ChunSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  ChunSnesVersion version;

 protected:
  uint16_t addrSampNumTable;
  uint16_t addrSampleTable;
  uint32_t spcDirAddr;
  std::vector<uint8_t> usedSRCNs;
};

// *************
// ChunSnesInstr
// *************

class ChunSnesInstr
    : public VGMInstr {
 public:
  ChunSnesInstr(VGMInstrSet *instrSet,
                ChunSnesVersion ver,
                uint8_t theInstrNum,
                uint16_t addrInstr,
                uint16_t addrSampleTable,
                uint32_t spcDirAddr,
                const std::string &name = "ChunSnesInstr");
  ~ChunSnesInstr() override;

  bool loadInstr() override;

  ChunSnesVersion version;

 protected:
  uint16_t addrSampleTable;
  uint32_t spcDirAddr;
};

// ***********
// ChunSnesRgn
// ***********

class ChunSnesRgn
    : public VGMRgn {
 public:
  ChunSnesRgn(ChunSnesInstr *instr, ChunSnesVersion ver, uint8_t srcn, uint16_t addrRgn, uint32_t spcDirAddr);
  ~ChunSnesRgn() override;

  bool loadRgn() override;

  ChunSnesVersion version;
};
