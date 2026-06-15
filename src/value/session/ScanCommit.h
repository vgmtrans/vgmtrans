/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/session/AssetStore.h"
#include "value/session/DiagnosticStore.h"
#include "value/session/MatchFactStore.h"

namespace vgmtrans::core {

// Stages one module's scan output until it has been validated as a whole. This is
// deliberately small: it prevents logical partial commits without becoming a
// general rollback or transaction framework.
struct ScanCommit {
  SourceId source;
  u64 sourceSize = 0;
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<Diagnostic> diagnostics;
  std::vector<ExtractedSource> extractedSources;

  [[nodiscard]] static ScanCommit fromScanResult(const SourceFile& source, ScanResult result);

  void validate(const SourceStore& sources, const AssetStore& existingAssets) const;
  void commit(AssetStore& assets, MatchFactStore& matchFacts, DiagnosticStore& diagnostics);
};

}  // namespace vgmtrans::core
