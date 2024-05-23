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
  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForRareSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadDIR;
  static BytePattern ptnReadSRCNTable;
  static BytePattern ptnSongLoadDKC;
  static BytePattern ptnSongLoadDKC2;
  static BytePattern ptnVCmdExecDKC;
  static BytePattern ptnVCmdExecDKC2;
};
