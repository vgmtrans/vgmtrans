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
  void scan(RawFile *file, void *info) override;
  static void searchForGraphResSnesFromARAM(RawFile *file);

 private:
  static std::map<uint8_t, uint8_t> getInitDspRegMap(const RawFile *file);

  static BytePattern ptnLoadSeq;
  static BytePattern ptnDspRegInit;
};
