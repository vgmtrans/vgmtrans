/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "CPS2Scanner.h"

struct MAMEGame;

class CPS1Scanner : public VGMScanner {
public:
  void scan(RawFile* file, void *info) override;
private:
  static void loadCPS1(MAMEGame* gameentry, CPSFormatVer fmt_ver);
};
