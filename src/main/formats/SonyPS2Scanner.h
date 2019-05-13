/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"

class SonyPS2Scanner:
    public VGMScanner {
 public:
  SonyPS2Scanner(void) {
    USE_EXTENSION(L"sq")
    USE_EXTENSION(L"hd")
    USE_EXTENSION(L"bd")
  }
 public:
  ~SonyPS2Scanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForSeq(RawFile *file);
  void SearchForInstrSet(RawFile *file);
  void SearchForSampColl(RawFile *file);
};
