/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/validation/ValidationReport.h"

#include <span>

namespace vgmtrans::core {

class SourceStore;

// Admission check for one scanner result after IDs and item trees are normalized.
// Session calls this before accepting assets, match facts, diagnostics, or derived sources.
[[nodiscard]] ValidationReport validateScanResult(SourceId source, const ScanResult& result, const SourceStore& sources,
                                                  std::span<const Asset> existingAssets);

}  // namespace vgmtrans::core
