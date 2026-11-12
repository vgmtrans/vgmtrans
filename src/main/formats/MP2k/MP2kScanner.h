/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "Scanner.h"

#include <optional>

/* Scanner for the MP2K (aka Sappy) GBA format */
class MP2kScanner final : public VGMScanner {
public:
  explicit MP2kScanner(Format* format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = 0) override;

private:
  struct EngineLocation {
    size_t settingsOffset;
    u32 songTableOffset;
  };

  std::optional<EngineLocation> detectMP2K(RawFile *file);
  static bool isValidOffset(u32 offset, u32 romsize);
  static bool isGbaRomAddress(u32 address);
  static u32 gbaAddressToOffset(u32 address);
};
