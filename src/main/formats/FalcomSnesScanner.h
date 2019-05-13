/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"
#include "BytePattern.h"

class FalcomSnesScanner:
    public VGMScanner {
 public:
  FalcomSnesScanner(void) {
    USE_EXTENSION(L"spc");
  }
  virtual ~FalcomSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForFalcomSnesFromARAM(RawFile *file);
  void SearchForFalcomSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadInstr;
};
