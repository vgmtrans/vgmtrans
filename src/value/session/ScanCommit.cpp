/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/ScanCommit.h"

#include "value/validation/ScanValidation.h"

#include <utility>

namespace vgmtrans::core {

ScanCommit ScanCommit::fromScanResult(const SourceFile& sourceFile, ScanResult result) {
  ScanCommit commit{
      .source = sourceFile.id,
      .sourceSize = sourceFile.size,
      .assets = std::move(result.assets),
      .matchFacts = std::move(result.matchFacts),
      .explicitCollections = std::move(result.explicitCollections),
      .sourceMap = std::move(result.sourceMap),
      .diagnostics = std::move(result.diagnostics),
      .extractedSources = std::move(result.extractedSources),
  };

  for (auto& diagnostic : commit.diagnostics) {
    if (!diagnostic.range) {
      diagnostic.range = SourceRange{.source = commit.source, .offset = 0, .size = commit.sourceSize};
    }
  }

  return commit;
}

void ScanCommit::validate(const SourceStore& sources, const AssetStore& existingAssets) const {
  validateScanCommit(*this, sources, existingAssets).throwIfErrors();
}

void ScanCommit::commit(AssetStore& assetStore, MatchFactStore& matchFactStore,
                        ExplicitCollectionStore& explicitCollectionStore, SourceMapStore& sourceMapStore,
                        DiagnosticStore& diagnosticStore) {
  assetStore.append(std::move(assets), source);
  matchFactStore.append(std::move(matchFacts));
  explicitCollectionStore.append(std::move(explicitCollections), source);
  sourceMapStore.append(std::move(sourceMap));
  diagnosticStore.append(std::move(diagnostics));
}

}  // namespace vgmtrans::core
