/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/scan/SourceExtractor.h"
#include "value/validation/ValidationReport.h"

namespace vgmtrans::core {

class SourceStore;

// Admission check for one scanner result after IDs and item trees are normalized.
// Session calls this before accepting assets, match facts, diagnostics, or derived sources.
[[nodiscard]] ValidationReport validateScanResult(SourceId source, const ScanResult& result, const SourceStore& sources,
                                                  const SharedSequence<Asset>& existingAssets);

// Admission check for sources and diagnostics produced by one extractor.
[[nodiscard]] ValidationReport validateExtractionResult(SourceId source, const ExtractionResult& result,
                                                        const SourceStore& sources);

}  // namespace vgmtrans::core
