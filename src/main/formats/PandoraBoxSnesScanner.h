#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class PandoraBoxSnesScanner:
    public VGMScanner {
 public:
  PandoraBoxSnesScanner(void) {
    USE_EXTENSION("spc");
  }
  virtual ~PandoraBoxSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForPandoraBoxSnesFromARAM(RawFile *file);
  void SearchForPandoraBoxSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeqKKO;
  static BytePattern ptnLoadSeqTSP;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadSRCN;
};
