/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "BytePattern.h"
#include "Scanner.h"

#include <map>

class GraphResSnesScanner : public VGMScanner {
 public:
  explicit GraphResSnesScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info) override;
  static void searchForGraphResSnesFromARAM(RawFile *file);

 private:
  static std::map<u8, u8> getInitDspRegMap(const RawFile *file);

  static BytePattern ptnLoadSeq;
  static BytePattern ptnDspRegInit;
};
