/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "common.h"
#include "Scanner.h"

class KonamiGXScanner:
    public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);
  void LoadSeqTable(RawFile *file, uint32_t offset);
};



