#pragma once
#include "Scanner.h"
#include "common.h"

struct MAMEGame;

enum CPS2FormatVer: u8 {
  CPS2_VERSION_UNDEFINED,
  CPS2_V100,
  CPS2_V101,
  CPS2_V103,
  CPS2_V104,
  CPS2_V105A,
  CPS2_V105C,
  CPS2_V105,
  CPS2_V106B,
  CPS2_V115C,
  CPS2_V115,
  CPS2_V116B,
  CPS2_V116,
  CPS2_V130,
  CPS2_V131,
  CPS2_V140,
  CPS2_V171,
  CPS2_V180,
  CPS2_V200,
  CPS2_V201B,
  CPS2_V210,
  CPS2_V211,
  CPS3
};

CPS2FormatVer cps2VersionEnum(const std::string &versionStr);

class CPS2Scanner : public VGMScanner {
public:
  explicit CPS2Scanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile* file, void *info) override;
};


