/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum HudsonSnesVersion : uint8_t;  // see HudsonSnesFormat.h

class HudsonSnesScanner : public VGMScanner {
 public:
  explicit HudsonSnesScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForHudsonSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnNoteLenTable;
  static BytePattern ptnGetSeqTableAddrV0;
  static BytePattern ptnGetSeqTableAddrV1V2;
  static BytePattern ptnLoadTrackAddress;
  static BytePattern ptnLoadDIRV0;
};
