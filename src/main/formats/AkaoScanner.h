#pragma once
#include "Scanner.h"
#include "AkaoFormatVersion.h"

class AkaoScanner final :
    public VGMScanner {
 public:
  void Scan(RawFile *file, void *info = 0) override;

 private:
  static AkaoPs1Version DetermineVersionFromTag(RawFile *file) noexcept;
};
