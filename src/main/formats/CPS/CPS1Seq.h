#pragma once

#include "CPS1Scanner.h"
#include "VGMSeq.h"

class CPS1Seq:
    public VGMSeq {
public:
  CPS1Seq(RawFile *file, uint32_t offset, CPS1FormatVer fmtVersion, std::string name = {}, std::vector<int8_t> instrTransposeTable = {});
  ~CPS1Seq() = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  int8_t transposeForInstr(uint8_t instrIndex);
  uint8_t masterVolume() { return masterVol; }
  void setMasterVolume(uint8_t vol) { masterVol = vol; }
  CPS1FormatVer formatVersion() { return fmtVersion; }

private:
  std::vector<int8_t> instrTransposeTable;
  uint8_t masterVol = 0x7F;
  CPS1FormatVer fmtVersion;
};
