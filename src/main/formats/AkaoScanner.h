/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"

class AkaoScanner:
    public VGMScanner {
 public:
  AkaoScanner(void);
 public:
  virtual ~AkaoScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);
};
