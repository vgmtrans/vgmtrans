#pragma once

#include "CPS1Scanner.h"
#include "VGMSeq.h"

class CPS1Seq:
    public VGMSeq {
public:
  CPS1Seq(RawFile *file, uint32_t offset, CPS1FormatVer fmtVersion, std::string name = {}, std::vector<s8> instrTransposeTable = {});
  ~CPS1Seq() = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  s8 transposeForInstr(u8 instrIndex);
  u8 masterVolume() { return masterVol; }
  void setMasterVolume(u8 vol) { masterVol = vol; }
  CPS1FormatVer formatVersion() { return fmtVersion; }

private:
  std::vector<s8> instrTransposeTable;
  uint8_t masterVol = 0x7F;
  CPS1FormatVer fmtVersion;
};
