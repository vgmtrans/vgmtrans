#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class SoftCreatSnesScanner:
    public VGMScanner {
 public:
  SoftCreatSnesScanner(void) {
    USE_EXTENSION(L"spc");
  }
  virtual ~SoftCreatSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForSoftCreatSnesFromARAM(RawFile *file);
  void SearchForSoftCreatSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnVCmdExec;
};
