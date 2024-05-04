/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum MoriSnesVersion : uint8_t;  // see MoriSnesFormat.h

class MoriSnesScanner : public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForMoriSnesFromARAM(RawFile *file);
  void SearchForMoriSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnSetDIR;
};
