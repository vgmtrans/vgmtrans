/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class RareSnesScanner : public VGMScanner {
 public:
  virtual void scan(RawFile *file, void *info = 0);
  void searchForRareSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadDIR;
  static BytePattern ptnReadSRCNTable;
  static BytePattern ptnSongLoadDKC;
  static BytePattern ptnSongLoadDKC2;
  static BytePattern ptnVCmdExecDKC;
  static BytePattern ptnVCmdExecDKC2;
};
