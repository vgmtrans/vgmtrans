#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CPS2Format.h"
#include "CPS2FormatVersion.h"
#include "SeqTrack.h"
#include "VGMSeq.h"
class CPS2Seq:
    public VGMSeq {
public:
  CPS2Seq(RawFile *file, uint32_t offset, CPS2FormatVer fmt_version, std::string name, std::vector<s8> instrTransposeTable = {});
  ~CPS2Seq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  bool postLoad() override;
  s8 transposeForInstr(u8 instrIndex);
  u8 masterVolume() { return masterVol; }
  void setMasterVolume(u8 vol) { masterVol = vol; }

public:
  CPS2FormatVer fmt_version;

private:
  std::vector<s8> instrTransposeTable;
  u8 masterVol = 127;
};
