#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSampColl.h"

#include <string>
#include <vector>

// ****************
// CapcomSnesInstrSet
// ****************

class CapcomSnesInstrSet:
    public VGMInstrSet {
 public:
  CapcomSnesInstrSet
      (RawFile *file, u32 offset, u32 spcDirAddr, const std::string &name = "CapcomSnesInstrSet");
  ~CapcomSnesInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

 protected:
  u32 spcDirAddr;
  std::vector<u8> usedSRCNs;
};

// *************
// CapcomSnesInstr
// *************

class CapcomSnesInstr
    : public VGMInstr {
 public:
  CapcomSnesInstr(VGMInstrSet *instrSet,
                  u32 offset,
                  u32 theBank,
                  u32 theInstrNum,
                  u32 spcDirAddr,
                  const std::string &name = "CapcomSnesInstr");
  ~CapcomSnesInstr() override;

  bool loadInstr() override;

  static bool isValidHeader(RawFile *file, u32 addrInstrHeader, u32 spcDirAddr, bool validateSample);

 protected:
  u32 spcDirAddr;
};

// ***********
// CapcomSnesRgn
// ***********

class CapcomSnesRgn
    : public VGMRgn {
 public:
  CapcomSnesRgn(CapcomSnesInstr *instr, u32 offset);
  ~CapcomSnesRgn() override;

  bool loadRgn() override;
};
