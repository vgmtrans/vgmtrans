/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/ScanCommit.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] const Asset* batchAsset(AssetId id, const std::vector<Asset>& assets) {
  const auto found = std::ranges::find_if(assets, [id](const Asset& asset) { return metadata(asset).id == id; });
  return found != assets.end() ? &*found : nullptr;
}

[[nodiscard]] const Asset* scanVisibleAsset(AssetId id, const std::vector<Asset>& batchAssets,
                                            const AssetStore& existingAssets) {
  if (const auto* asset = batchAsset(id, batchAssets)) {
    return asset;
  }
  return existingAssets.asset(id);
}

[[nodiscard]] std::string roleName(CollectionMemberRole role) {
  switch (role) {
    case CollectionMemberRole::Sequence:
      return "sequence";
    case CollectionMemberRole::InstrumentSet:
      return "instrument-set";
    case CollectionMemberRole::SampleCollection:
      return "sample-collection";
    case CollectionMemberRole::Misc:
      return "misc";
  }
  return "unknown";
}

[[nodiscard]] std::string roleAssetName(CollectionMemberRole role) {
  switch (role) {
    case CollectionMemberRole::Sequence:
      return "sequence asset";
    case CollectionMemberRole::InstrumentSet:
      return "instrument set asset";
    case CollectionMemberRole::SampleCollection:
      return "sample collection asset";
    case CollectionMemberRole::Misc:
      return "misc asset";
  }
  return "asset";
}

[[nodiscard]] bool assetMatchesRole(const Asset& asset, CollectionMemberRole role) {
  switch (role) {
    case CollectionMemberRole::Sequence:
      return std::holds_alternative<SequenceProgramAsset>(asset);
    case CollectionMemberRole::InstrumentSet:
      return std::holds_alternative<InstrumentSetAsset>(asset);
    case CollectionMemberRole::SampleCollection:
      return std::holds_alternative<SampleCollectionAsset>(asset);
    case CollectionMemberRole::Misc:
      return std::holds_alternative<MiscAsset>(asset);
  }
  return false;
}

void validateCollectionMemberFact(const MatchFact& fact, const CollectionMemberFact& member,
                                  const std::vector<Asset>& batchAssets, const AssetStore& existingAssets) {
  const auto* asset = scanVisibleAsset(fact.asset, batchAssets, existingAssets);
  if (asset == nullptr) {
    throw std::invalid_argument("Scan result contained a collection member fact for missing asset id " +
                                std::to_string(fact.asset.value));
  }
  if (!assetMatchesRole(*asset, member.role)) {
    throw std::invalid_argument("Scan result contained a collection member fact with " + roleName(member.role) +
                                " role for asset id " + std::to_string(fact.asset.value) +
                                ", but that asset is not a " + roleAssetName(member.role));
  }
}

template <typename T>
[[nodiscard]] bool assetIs(AssetId id, const std::vector<Asset>& batchAssets, const AssetStore& existingAssets) {
  const auto* asset = scanVisibleAsset(id, batchAssets, existingAssets);
  return asset != nullptr && std::holds_alternative<T>(*asset);
}

void validateProgramAssetReferences(const SequenceProgramAsset& sequence, const std::vector<Asset>& batchAssets,
                                    const AssetStore& existingAssets) {
  for (const auto& reference : sequence.program.referencedInstruments) {
    if (!reference.asset) {
      continue;
    }
    if (!reference.asset->valid() || !assetIs<InstrumentSetAsset>(*reference.asset, batchAssets, existingAssets)) {
      throw std::invalid_argument("Scan result contained sequence instrument reference to asset id " +
                                  std::to_string(reference.asset->value) +
                                  ", but that asset is not an instrument set asset");
    }
  }
}

void validateInstrumentSetAssetReferences(const InstrumentSetAsset& instrumentSet,
                                          const std::vector<Asset>& batchAssets, const AssetStore& existingAssets) {
  for (const auto& instrument : instrumentSet.instruments) {
    for (const auto& region : instrument.regions) {
      if (!region.sample.collection) {
        continue;
      }
      if (!region.sample.collection->valid() ||
          !assetIs<SampleCollectionAsset>(*region.sample.collection, batchAssets, existingAssets)) {
        throw std::invalid_argument("Scan result contained instrument region sample collection reference to asset id " +
                                    std::to_string(region.sample.collection->value) +
                                    ", but that asset is not a sample collection asset");
      }
    }
  }
}

void validateAssetReferences(const Asset& asset, const std::vector<Asset>& batchAssets,
                             const AssetStore& existingAssets) {
  if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset)) {
    validateProgramAssetReferences(*sequence, batchAssets, existingAssets);
  } else if (const auto* instrumentSet = std::get_if<InstrumentSetAsset>(&asset)) {
    validateInstrumentSetAssetReferences(*instrumentSet, batchAssets, existingAssets);
  }
}

}  // namespace

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
    validateAssetReferences(asset, assets, existingAssets);
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
    if (const auto* member = std::get_if<CollectionMemberFact>(&fact.payload)) {
      validateCollectionMemberFact(fact, *member, assets, existingAssets);
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
