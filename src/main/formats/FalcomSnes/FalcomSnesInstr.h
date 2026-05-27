#pragma once

#include "base/Types.h"
#include "FalcomSnesFormat.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"

#include <map>
#include <string>
#include <vector>

class FalcomSnesInstrSet;
class FalcomSnesInstr;
class FalcomSnesRgn;

// ******************
// FalcomSnesInstrSet
// ******************

class FalcomSnesInstrSet:
    public VGMInstrSet {
 public:
  friend FalcomSnesInstr;
  friend FalcomSnesRgn;

  FalcomSnesInstrSet(RawFile *file,
                     FalcomSnesVersion ver,
                     u32 offset,
                     u32 addrSampToInstrTable,
                     u32 spcDirAddr,
                     const std::map<u8, u16> &instrADSRHints,
                     const std::string &name = "FalcomSnesInstrSet");
  ~FalcomSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  FalcomSnesVersion version;

 protected:
  u32 spcDirAddr;
  u32 addrSampToInstrTable;
  std::vector<u8> usedSRCNs;
  std::map<u8, u16> instrADSRHints;
};

// *************
// FalcomSnesInstr
// *************

class FalcomSnesInstr
    : public VGMInstr {
 public:
  FalcomSnesInstr(VGMInstrSet *instrSet,
                  FalcomSnesVersion ver,
                  u32 offset,
                  u32 theBank,
                  u32 theInstrNum,
                  u8 srcn,
                  u32 spcDirAddr,
                  const std::string &name = "FalcomSnesInstr");
  ~FalcomSnesInstr() override;

  bool loadInstr() override;

  FalcomSnesVersion version;

 protected:
  u8 srcn;
  u32 spcDirAddr;
};

// ***********
// FalcomSnesRgn
// ***********

class FalcomSnesRgn
    : public VGMRgn {
 public:
  FalcomSnesRgn(FalcomSnesInstr *instr,
             FalcomSnesVersion ver,
             u32 offset,
             u8 srcn);
  ~FalcomSnesRgn() override;

  bool loadRgn() override;

  FalcomSnesVersion version;
};
