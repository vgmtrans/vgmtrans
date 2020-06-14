#pragma once
#include "Scanner.h"
#include "AkaoFormatVersion.h"

class AkaoScanner:
    public VGMScanner {
 public:
  AkaoScanner(void);
 public:
  virtual ~AkaoScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);

 private:
  AkaoPs1Version DetermineVersionFromTag(RawFile *file);
};
