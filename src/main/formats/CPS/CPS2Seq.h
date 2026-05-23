#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPS2Format.h"

enum CPS2FormatVer: uint8_t;

class CPS2Seq:
    public VGMSeq {
public:
  CPS2Seq(RawFile *file, uint32_t offset, CPS2FormatVer fmt_version, std::string name, std::vector<int8_t> instrTransposeTable = {});
  ~CPS2Seq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  bool postLoad() override;
  int8_t transposeForInstr(uint8_t instrIndex);
  uint8_t masterVolume() { return masterVol; }
  void setMasterVolume(uint8_t vol) { masterVol = vol; }

public:
  CPS2FormatVer fmt_version;

private:
  std::vector<int8_t> instrTransposeTable;
  uint8_t masterVol = 127;
};
