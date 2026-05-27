#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"

// ************************
// AsciiShuichiSnesInstrSet
// ************************

class AsciiShuichiSnesInstrSet:
    public VGMInstrSet {
 public:
  AsciiShuichiSnesInstrSet
      (RawFile *file, u32 offset, u32 fineTuningTableAddress, u32 spcDirAddress, const std::string &name = "AsciiShuichiSnesInstrSet");

  bool parseHeader() override;
  bool parseInstrPointers() override;

 private:
  u32 fineTuningTableAddress;
  u32 spcDirAddress;
  std::vector<u8> usedSRCNs;
};

// *********************
// AsciiShuichiSnesInstr
// *********************

class AsciiShuichiSnesInstr
    : public VGMInstr {
 public:
  AsciiShuichiSnesInstr(VGMInstrSet *instrSet,
                  u32 offset,
                  u32 theBank,
                  u32 theInstrNum,
                  u32 spcDirAddress,
                  u32 fineTuningTableAddress,
                  const std::string &name = "AsciiShuichiSnesInstr");

  bool loadInstr() override;

  static bool isValidHeader(const RawFile *file, u32 instrHeaderAddress, u32 spcDirAddress);

 private:
  u32 spcDirAddress;
  u32 fineTuningTableAddress;
};

// *******************
// AsciiShuichiSnesRgn
// *******************

class AsciiShuichiSnesRgn
    : public VGMRgn {
 public:
  AsciiShuichiSnesRgn(AsciiShuichiSnesInstr *instr, u32 offset, s8 spcFineTuning);

  bool loadRgn() override;
};
