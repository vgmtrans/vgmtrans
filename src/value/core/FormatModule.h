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

class FormatModule {
 public:
  virtual ~FormatModule() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  // canScan should be cheap and non-mutating; scan does the full parse once selected.
  [[nodiscard]] virtual bool canScan(const SourceFile& source, std::span<const u8> bytes) const = 0;
  [[nodiscard]] virtual ScanResult scan(const ScanInput& input) const = 0;
};

}  // namespace vgmtrans::core
