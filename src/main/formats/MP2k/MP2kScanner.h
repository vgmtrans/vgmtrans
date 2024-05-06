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
  void Scan(RawFile *file, void *info = 0) override;

private:
  std::optional<size_t> DetectMP2K(RawFile *file);
  static bool IsValidOffset(u32 offset, u32 romsize);
  static bool IsGBAROMAddress(u32 address);
  static u32 GBAAddressToOffset(u32 address);
};
