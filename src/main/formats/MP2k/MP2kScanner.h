/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "Scanner.h"
#include "common.h"
#include <optional>

/* Scanner for the MP2K (aka Sappy) GBA format */
class MP2kScanner final : public VGMScanner {
public:
  explicit MP2kScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = 0) override;

private:
  std::optional<size_t> detectMP2K(RawFile *file);
  static bool isValidOffset(uint32_t offset, uint32_t romsize);
  static bool isGbaRomAddress(uint32_t address);
  static uint32_t gbaAddressToOffset(uint32_t address);
};
