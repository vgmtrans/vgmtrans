/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/ScanCommit.h"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

ScanCommit ScanCommit::fromScanResult(const SourceFile& sourceFile, ScanResult result) {
  ScanCommit commit{
      .source = sourceFile.id,
      .sourceSize = sourceFile.size,
      .assets = std::move(result.assets),
      .matchFacts = std::move(result.matchFacts),
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
  if (!sources.contains(source)) {
    throw std::invalid_argument("Scan result source is not active");
  }

  std::unordered_set<u32> batchAssetIds;
  for (const auto& asset : assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      throw std::invalid_argument("Scan result contained an asset without an id");
    }
    if (!batchAssetIds.insert(id.value).second) {
      throw std::invalid_argument("Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (existingAssets.contains(id)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (const auto& fact : matchFacts) {
    if (!fact.asset.valid()) {
      throw std::invalid_argument("Scan result contained a match fact without an asset id");
    }
    if (!batchAssetIds.contains(fact.asset.value) && !existingAssets.contains(fact.asset)) {
      throw std::invalid_argument("Scan result contained a match fact for missing asset id " +
                                  std::to_string(fact.asset.value));
    }
    if (fact.scope.kind == MatchScopeKind::Source && !fact.scope.source) {
      throw std::invalid_argument("Scan result contained a source-scoped match fact without a source id");
    }
    if (fact.scope.source && !sources.contains(*fact.scope.source)) {
      throw std::invalid_argument("Scan result contained a match fact for missing source id " +
                                  std::to_string(fact.scope.source->value));
    }
  }

  for (const auto& extracted : extractedSources) {
    if (extracted.origin && extracted.origin->source.valid() && !sources.contains(extracted.origin->source)) {
      throw std::invalid_argument("Scan result contained extracted source with missing parent source " +
                                  std::to_string(extracted.origin->source.value));
    }
  }
}

void ScanCommit::commit(AssetStore& assetStore, MatchFactStore& matchFactStore, DiagnosticStore& diagnosticStore) {
  assetStore.append(std::move(assets), source);
  matchFactStore.append(std::move(matchFacts));
  diagnosticStore.append(std::move(diagnostics));
}

}  // namespace vgmtrans::core
