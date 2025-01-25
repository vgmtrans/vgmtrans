/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class AsciiShuichiSnesScanner : public VGMScanner {
 public:
  explicit AsciiShuichiSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info) override;
  static void scanFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnLoadInstr;
  static BytePattern ptnLoadDIR;
};
