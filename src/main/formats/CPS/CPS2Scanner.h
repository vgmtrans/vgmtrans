#pragma once

#include <string>

#include "CPS2FormatVersion.h"
#include "Scanner.h"
struct MAMEGame;

CPS2FormatVer cps2VersionEnum(const std::string &versionStr);

class CPS2Scanner : public VGMScanner {
public:
  explicit CPS2Scanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile* file, void *info) override;
};

