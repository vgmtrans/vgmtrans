#pragma once

#include "VGMSeq.h"

class CPS0Seq:
    public VGMSeq {
public:
  CPS0Seq(RawFile *file, uint32_t offset, std::string name = {}, std::vector<s8> instrTransposeTable = {});
  ~CPS0Seq() = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  s8 getTransposeForInstr(u8 instrIndex);
  u8 masterVolume() { return masterVol; }
  void setMasterVolume(u8 vol) { masterVol = vol; }

private:
  std::vector<s8> instrTransposeTable;
  uint8_t masterVol = 0x7F;
};
