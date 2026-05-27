#pragma once

#include "base/Types.h"
#include "CPS2Format.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <string>
#include <vector>

enum CPS2FormatVer: u8;

class CPS2Seq:
    public VGMSeq {
public:
  CPS2Seq(RawFile *file, u32 offset, CPS2FormatVer fmt_version, std::string name, std::vector<s8> instrTransposeTable = {});
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
