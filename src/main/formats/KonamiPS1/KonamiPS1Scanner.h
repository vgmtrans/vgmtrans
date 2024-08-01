/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class KonamiPS1Scanner : public VGMScanner {
 public:
  explicit KonamiPS1Scanner(Format* format) : VGMScanner(format) {}
  ~KonamiPS1Scanner() override = default;

  virtual void scan(RawFile *file, void *info = 0);
};
