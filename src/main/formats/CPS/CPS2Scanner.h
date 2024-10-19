#pragma once
#include "Scanner.h"

struct MAMEGame;

enum CPSFormatVer: uint8_t {
  VERSION_UNDEFINED,
  CPS_FM_V100,
  CPS_FM_V200,
  CPS_FM_V350,
  CPS_FM_V425,
  CPS_FM_V500,
  CPS_FM_V502,
  CPS_QSOUND_V100,
  CPS_QSOUND_V101,
  CPS_QSOUND_V103,
  CPS_QSOUND_V104,
  CPS_QSOUND_V105A,
  CPS_QSOUND_V105C,
  CPS_QSOUND_V105,
  CPS_QSOUND_V106B,
  CPS_QSOUND_V115C,
  CPS_QSOUND_V115,
  CPS_QSOUND_V116B,
  CPS_QSOUND_V116,
  CPS_QSOUND_V130,
  CPS_QSOUND_V131,
  CPS_QSOUND_V140,
  CPS_QSOUND_V171,
  CPS_QSOUND_V180,
  CPS_QSOUND_V200,
  CPS_QSOUND_V201B,
  CPS_QSOUND_V210,
  CPS_QSOUND_V211,
  CPS3
};

CPSFormatVer versionEnum(const std::string &versionStr);

class CPS2Scanner : public VGMScanner {
public:
  explicit CPS2Scanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile* file, void *info) override;
};


