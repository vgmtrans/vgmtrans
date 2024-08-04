/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Scanner.h"
#include "AkaoFormatVersion.h"

class AkaoScanner final : public VGMScanner {
 public:
  explicit AkaoScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = nullptr) override;

 private:
  static AkaoPs1Version determineVersionFromTag(const RawFile *file) noexcept;
};
