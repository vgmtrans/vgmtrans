#pragma once
#include "Scanner.h"

class JaiSeqBMSScanner :
  public VGMScanner {
public:
  JaiSeqBMSScanner(void) {
    USE_EXTENSION(L"bms")
  }
  virtual ~JaiSeqBMSScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};

class JaiSeqAAFScanner :
  public VGMScanner {
public:
  JaiSeqAAFScanner(void) {
    USE_EXTENSION(L"aaf")
  }
  virtual ~JaiSeqAAFScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};

class JaiSeqBAAScanner :
  public VGMScanner {
public:
  JaiSeqBAAScanner(void) {
    USE_EXTENSION(L"baa")
  }
  virtual ~JaiSeqBAAScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
};
