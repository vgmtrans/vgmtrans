/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class GraphResSnesScanner : public VGMScanner {
 public:
  void Scan(RawFile *file, void *info) override;
  static void SearchForGraphResSnesFromARAM(RawFile *file);
  static void SearchForGraphResSnesFromROM(RawFile *file);

 private:
  static std::map<uint8_t, uint8_t> GetInitDspRegMap(const RawFile *file);

  static BytePattern ptnLoadSeq;
  static BytePattern ptnDspRegInit;
};
