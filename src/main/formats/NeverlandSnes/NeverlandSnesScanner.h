/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class NeverlandSnesScanner : public VGMScanner {
 public:
  explicit NeverlandSnesScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForNeverlandSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSongSFC;
  static BytePattern ptnLoadSongS2C;
};
