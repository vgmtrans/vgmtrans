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
  virtual ~FFTScanner();

  virtual void Scan(RawFile *file, void *info = 0);  // scan "smds" and "wds"
  void SearchForFFTSeq(RawFile *file);               // scan "smds"
  void SearchForFFTwds(RawFile *file);               // scan "wds"
};
