#pragma once
#include "VGMInstrSet.h"
#include "VGMRgn.h"

// ************************
// AsciiShuichiSnesInstrSet
// ************************

class AsciiShuichiSnesInstrSet:
    public VGMInstrSet {
 public:
  AsciiShuichiSnesInstrSet
      (RawFile *file, uint32_t offset, uint32_t fineTuningTableAddress, uint32_t spcDirAddress, const std::string &name = "AsciiShuichiSnesInstrSet");

  bool parseHeader() override;
  bool parseInstrPointers() override;

 private:
  uint32_t fineTuningTableAddress;
  uint32_t spcDirAddress;
  std::vector<uint8_t> usedSRCNs;
};

// *********************
// AsciiShuichiSnesInstr
// *********************

class AsciiShuichiSnesInstr
    : public VGMInstr {
 public:
  AsciiShuichiSnesInstr(VGMInstrSet *instrSet,
                  uint32_t offset,
                  uint32_t theBank,
                  uint32_t theInstrNum,
                  uint32_t spcDirAddress,
                  uint32_t fineTuningTableAddress,
                  const std::string &name = "AsciiShuichiSnesInstr");

  bool loadInstr() override;

  static bool isValidHeader(const RawFile *file, uint32_t instrHeaderAddress, uint32_t spcDirAddress);

 private:
  uint32_t spcDirAddress;
  uint32_t fineTuningTableAddress;
};

// *******************
// AsciiShuichiSnesRgn
// *******************

class AsciiShuichiSnesRgn
    : public VGMRgn {
 public:
  AsciiShuichiSnesRgn(AsciiShuichiSnesInstr *instr, uint32_t offset, int8_t spcFineTuning);

  bool loadRgn() override;
};
