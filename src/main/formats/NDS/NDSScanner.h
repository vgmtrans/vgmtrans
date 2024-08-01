/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class NDSScanner : public VGMScanner {
 public:
  explicit NDSScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForSDAT(RawFile *file);
  uint32_t loadFromSDAT(RawFile *file, uint32_t offset);
};
