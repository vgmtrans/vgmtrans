#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class FalcomSnesScanner:
    public VGMScanner {
 public:
  FalcomSnesScanner(void) {
    USE_EXTENSION("spc");
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
