/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum SuzukiSnesVersion : uint8_t;  // see SuzukiSnesFormat.h

class SuzukiSnesScanner : public VGMScanner {
 public:
  virtual void scan(RawFile *file, void *info = 0);
  void searchForSuzukiSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSongSD3;
  static BytePattern ptnLoadSongBL;
  static BytePattern ptnExecVCmdBL;
  static BytePattern ptnLoadDIR;
  static BytePattern ptnLoadInstr;
};
