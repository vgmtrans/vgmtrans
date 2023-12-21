#pragma once
#include "Scanner.h"

class TamSoftPS1Scanner:
    public VGMScanner {
 public:
  TamSoftPS1Scanner(void) {
    USE_EXTENSION("tsq");
    USE_EXTENSION("tvb");
  }

  virtual ~TamSoftPS1Scanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};
