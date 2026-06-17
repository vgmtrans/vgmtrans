/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/validation/ValidationReport.h"

namespace vgmtrans::core {

class AssetStore;
class SourceStore;
struct ScanCommit;

// Admission check for one scanner result after IDs and item trees are normalized.
// Session calls this before accepting assets, match facts, diagnostics, or derived sources.
[[nodiscard]] ValidationReport validateScanCommit(const ScanCommit& commit, const SourceStore& sources,
                                                  const AssetStore& existingAssets);

}  // namespace vgmtrans::core
