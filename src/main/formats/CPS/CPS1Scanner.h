#pragma once
#include "Scanner.h"
#include "CPS2Scanner.h"

struct MAMEGame;

class CPS1Scanner:
    public VGMScanner {
public:
  virtual void Scan(RawFile* file, void *info = 0);
private:
  void LoadCPS1(MAMEGame* gameentry, CPSFormatVer fmt_ver);
};
