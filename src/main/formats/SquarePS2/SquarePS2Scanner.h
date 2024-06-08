/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class SquarePS2Scanner : public VGMScanner {
 public:
  SquarePS2Scanner();
  ~SquarePS2Scanner();

  virtual void scan(RawFile *file, void *info);
  void searchForBGMSeq(RawFile *file);
  void searchForWDSet(RawFile *file);
};

