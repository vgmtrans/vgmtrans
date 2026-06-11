/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/core/FormatModule.h"
#include "value/core/FormatRegistry.h"

#include <span>
#include <string_view>

namespace vgmtrans::formats::snes_rsn {

class SnesRsnExtractor final : public core::FormatModule {
 public:
  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] bool canScan(const core::SourceFile& source, std::span<const u8> bytes) const override;
  [[nodiscard]] core::ScanResult scan(const core::ScanInput& input) const override;
};

void registerSnesRsnExtractor(core::FormatRegistry& registry);

}  // namespace vgmtrans::formats::snes_rsn
