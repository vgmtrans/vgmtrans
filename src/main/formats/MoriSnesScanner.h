#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum MoriSnesVersion: uint8_t; // see MoriSnesFormat.h

class MoriSnesScanner:
    public VGMScanner {
 public:
  MoriSnesScanner(void) {
    USE_EXTENSION(L"spc");
  }
  virtual ~MoriSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForMoriSnesFromARAM(RawFile *file);
  void SearchForMoriSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnSetDIR;
};
