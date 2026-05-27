/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"

#include "Scanner.h"

#include <cstdint>

class NDSScanner : public VGMScanner {
 public:
  explicit NDSScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForSDAT(RawFile *file);
  u32 loadFromSDAT(RawFile *file, u32 offset);
};
