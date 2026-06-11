/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatModule.h"
#include "value/core/FormatRegistry.h"

namespace vgmtrans::formats::capcom_snes {

class CapcomSnesModule final : public core::FormatModule {
 public:
  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] bool canScan(const core::SourceFile& source, std::span<const u8> bytes) const override;
  [[nodiscard]] core::ScanResult scan(const core::ScanInput& input) const override;
};

void registerCapcomSnesModule(core::FormatRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes
