/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class TamSoftPS1Scanner : public VGMScanner {
 public:
  explicit TamSoftPS1Scanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
};
