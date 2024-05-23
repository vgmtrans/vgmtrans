/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class PandoraBoxSnesScanner : public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForPandoraBoxSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSeqKKO;
  static BytePattern ptnLoadSeqTSP;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadSRCN;
};
