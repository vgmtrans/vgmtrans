/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <span>

namespace vgmtrans::formats::akao {

[[nodiscard]] bool canScanAkao(const core::SourceFile& source, std::span<const u8> bytes);
[[nodiscard]] core::ScanResult scanAkao(const core::ScanInput& input);

}  // namespace vgmtrans::formats::akao
