/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class SonyPS2Scanner : public VGMScanner {
 public:
  explicit SonyPS2Scanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  void searchForSeq(RawFile *file);
  void searchForInstrSet(RawFile *file);
  void searchForSampColl(RawFile *file);
};
