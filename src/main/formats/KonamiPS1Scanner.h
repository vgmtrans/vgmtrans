#pragma once
#include "Scanner.h"

class KonamiPS1Scanner : public VGMScanner {
 public:
  KonamiPS1Scanner() {
  }

  virtual ~KonamiPS1Scanner() {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};
