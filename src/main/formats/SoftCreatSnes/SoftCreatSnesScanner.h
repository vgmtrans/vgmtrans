/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class SoftCreatSnesScanner : public VGMScanner {
 public:
  virtual void scan(RawFile *file, void *info = 0);
  void searchForSoftCreatSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnVCmdExec;
};
