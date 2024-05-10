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
  void Scan(RawFile *file, void *info = nullptr) override;

 private:
  static AkaoPs1Version DetermineVersionFromTag(const RawFile *file) noexcept;
};
