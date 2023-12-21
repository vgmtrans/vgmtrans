#pragma once
#include "Scanner.h"

class NDSScanner:
    public VGMScanner {
 public:
  NDSScanner(void) {
    USE_EXTENSION("nds")
    USE_EXTENSION("sdat")
  }
  virtual ~NDSScanner(void) {
  }


  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForSDAT(RawFile *file);
  uint32_t LoadFromSDAT(RawFile *file, uint32_t offset);
};
