/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"

class TamSoftPS1Scanner:
    public VGMScanner {
 public:
  TamSoftPS1Scanner(void) {
    USE_EXTENSION(L"tsq");
    USE_EXTENSION(L"tvb");
  }

  virtual ~TamSoftPS1Scanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};
