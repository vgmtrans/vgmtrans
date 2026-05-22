/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "BytePattern.h"
#include "Scanner.h"
enum MoriSnesVersion : uint8_t;  // see MoriSnesFormat.h

class MoriSnesScanner : public VGMScanner {
 public:
  explicit MoriSnesScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForMoriSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnLoadSeq;
  static BytePattern ptnSetDIR;
};
