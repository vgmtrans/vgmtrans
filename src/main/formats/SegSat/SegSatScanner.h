/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "common.h"
#include <span>

class SegSatScanner : public VGMScanner {
 public:
  explicit SegSatScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForSequences(RawFile *file);
  bool validateBankAt(RawFile* file, u32 base);
  void searchForInstrumentSets(RawFile *file);
};
