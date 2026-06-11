/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/ScanTypes.h"

#include <span>
#include <string_view>

namespace vgmtrans::core {

struct FormatModule {
  using CanScan = bool (*)(const SourceFile& source, std::span<const u8> bytes);
  using Scan = ScanResult (*)(const ScanInput& input);

  std::string_view name;
  // canScan should be cheap and non-mutating; scan does the full parse once selected.
  CanScan canScan = nullptr;
  Scan scan = nullptr;
};

}  // namespace vgmtrans::core
