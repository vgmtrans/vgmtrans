#pragma once
#include "Scanner.h"

class SegSatScanner:
    public VGMScanner {
 public:
  SegSatScanner(void);
 public:
  virtual ~SegSatScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);
};
