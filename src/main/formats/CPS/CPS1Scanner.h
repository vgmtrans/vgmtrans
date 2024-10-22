/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "CPS2Scanner.h"

struct MAMEGame;

enum CPS1FormatVer: uint8_t {
  CPS1_VERSION_UNDEFINED,
  CPS1_V100,
  CPS1_V200,
  CPS1_V350,
  CPS1_V425,
  CPS1_V500,
  CPS1_V502,
};

CPS1FormatVer cps1VersionEnum(const std::string &versionStr);

class CPS1Scanner : public VGMScanner {
public:
  explicit CPS1Scanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile* file, void *info) override;
private:
  static void loadCPS1(MAMEGame* gameentry, CPS1FormatVer fmt_ver);
};
