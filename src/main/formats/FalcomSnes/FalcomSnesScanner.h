/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class FalcomSnesScanner : public VGMScanner {
 public:
  void Scan(RawFile *file, void *info) override;
  static void SearchForFalcomSnesFromARAM(RawFile *file);
  static void SearchForFalcomSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnSetDIR;
  static BytePattern ptnLoadInstr;
};
