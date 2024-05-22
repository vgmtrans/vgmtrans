/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class FFTScanner : public VGMScanner {
 public:
  FFTScanner();
  ~FFTScanner() override;

  void Scan(RawFile *file, void *info) override;  // scan "smds" and "wds"
  static void SearchForFFTSeq(RawFile *file);               // scan "smds"
  static void SearchForFFTwds(RawFile *file);               // scan "wds"
};
