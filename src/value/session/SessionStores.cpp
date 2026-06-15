/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SessionStores.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string collectionKeyString(const CollectionKey& key) {
  return key.resolver + '\x1f' + key.value;
}

[[nodiscard]] std::string assetKindName(const Asset& asset) {
  return std::visit(
      [](const auto& typedAsset) -> std::string {
        using T = std::decay_t<decltype(typedAsset)>;
        if constexpr (std::is_same_v<T, SequenceProgramAsset>) {
          return "sequence";
        } else if constexpr (std::is_same_v<T, InstrumentSetAsset>) {
          return "instrument-set";
        } else if constexpr (std::is_same_v<T, SampleCollectionAsset>) {
          return "sample-collection";
        } else {
          return "misc";
        }
      },
      asset);
}

[[nodiscard]] std::string assetStableKey(const Asset& asset) {
  const auto& meta = metadata(asset);
  const SourceRange range = meta.range;
  // The generic key is intentionally source-backed. Formats can keep asset IDs
  // stable across rescans by preserving source range, kind, format, and local name.
  return meta.format + '\x1f' + assetKindName(asset) + '\x1f' + std::to_string(range.source.value) + '\x1f' +
         std::to_string(range.offset) + '\x1f' + std::to_string(range.size) + '\x1f' + meta.name;
}

void remapOptionalAsset(std::optional<AssetId>& id, const std::unordered_map<u32, AssetId>& remappedIds) {
  if (!id) {
    return;
  }
  if (const auto found = remappedIds.find(id->value); found != remappedIds.end()) {
    id = found->second;
  }
}

void remapAssetReferences(Asset& asset, const std::unordered_map<u32, AssetId>& remappedIds) {
  std::visit(
      [&](auto& typedAsset) {
        using T = std::decay_t<decltype(typedAsset)>;
        if constexpr (std::is_same_v<T, SequenceProgramAsset>) {
          for (auto& ref : typedAsset.program.referencedInstruments) {
            remapOptionalAsset(ref.asset, remappedIds);
          }
        } else if constexpr (std::is_same_v<T, InstrumentSetAsset>) {
          for (auto& instrument : typedAsset.instruments) {
            for (auto& region : instrument.regions) {
              remapOptionalAsset(region.sample.collection, remappedIds);
            }
          }
        }
      },
      asset);
}

[[nodiscard]] bool assetFromSource(const Asset& asset, const std::unordered_set<u32>& sourceIds) {
  const auto& meta = metadata(asset);
  return meta.range.valid() && sourceIds.contains(meta.range.source.value);
}

[[nodiscard]] bool referencesAsset(const Collection& collection, const std::unordered_set<u32>& assetIds) {
  const auto referencesOne = [&](std::optional<AssetId> id) { return id && assetIds.contains(id->value); };
  const auto referencesAny = [&](const std::vector<AssetId>& ids) {
    return std::ranges::any_of(ids, [&](AssetId id) { return assetIds.contains(id.value); });
  };
  return referencesOne(collection.sequence) || referencesAny(collection.instrumentSets) ||
         referencesAny(collection.sampleCollections) || referencesAny(collection.miscAssets);
}

}  // namespace

void AssetStore::clear() {
  assets_.clear();
  assetsByStableKey_.clear();
  retiredIdsByStableKey_.clear();
  assetsById_.clear();
}

AssetUpsertResult AssetStore::upsertDiscovered(std::vector<Asset> assets) {
  AssetUpsertResult result;

  for (const auto& asset : assets) {
    const auto& meta = metadata(asset);
    if (!meta.id.valid()) {
      continue;
    }

    const auto key = assetStableKey(asset);
    if (const auto found = assetsByStableKey_.find(key); found != assetsByStableKey_.end()) {
      result.remappedIds.emplace(meta.id.value, metadata(assets_[found->second]).id);
    } else if (const auto retired = retiredIdsByStableKey_.find(key); retired != retiredIdsByStableKey_.end()) {
      result.remappedIds.emplace(meta.id.value, retired->second);
    } else {
      result.remappedIds.emplace(meta.id.value, meta.id);
    }
  }

  for (auto& asset : assets) {
    auto& meta = metadata(asset);
    if (const auto found = result.remappedIds.find(meta.id.value); found != result.remappedIds.end()) {
      meta.id = found->second;
    }
    remapAssetReferences(asset, result.remappedIds);

    const auto key = assetStableKey(asset);
    if (const auto found = assetsByStableKey_.find(key); found != assetsByStableKey_.end()) {
      assets_[found->second] = std::move(asset);
    } else {
      retiredIdsByStableKey_.erase(key);
      assetsByStableKey_.emplace(key, assets_.size());
      assets_.push_back(std::move(asset));
    }
  }

  rebuildIndexes();
  return result;
}

std::unordered_set<u32> AssetStore::removeForSources(const std::unordered_set<u32>& sourceIds,
                                                     const std::unordered_set<u32>& additionalAssetIds) {
  std::unordered_set<u32> removed = additionalAssetIds;
  std::erase_if(assets_, [&](const Asset& asset) {
    const auto id = metadata(asset).id;
    if (id.valid() && removed.contains(id.value)) {
      retiredIdsByStableKey_[assetStableKey(asset)] = id;
      return true;
    }
    if (!assetFromSource(asset, sourceIds)) {
      return false;
    }
    if (id.valid()) {
      removed.insert(id.value);
      retiredIdsByStableKey_[assetStableKey(asset)] = id;
    }
    return true;
  });
  rebuildIndexes();
  return removed;
}

void AssetStore::rebuildIndexes() {
  assetsByStableKey_.clear();
  assetsById_.clear();
  for (size_t i = 0; i < assets_.size(); ++i) {
    assetsByStableKey_.emplace(assetStableKey(assets_[i]), i);
    const auto id = metadata(assets_[i]).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, i);
    }
  }
}

void MatchFactStore::clear() {
  facts_.clear();
}

void MatchFactStore::add(std::vector<MatchFact> facts, const std::unordered_map<u32, AssetId>& remappedIds) {
  for (auto& fact : facts) {
    if (const auto found = remappedIds.find(fact.asset.value); found != remappedIds.end()) {
      fact.asset = found->second;
    }
    facts_.push_back(std::move(fact));
  }
}

std::unordered_set<u32> MatchFactStore::assetIdsForSources(const std::unordered_set<u32>& sourceIds) const {
  std::unordered_set<u32> assetIds;
  for (const auto& fact : facts_) {
    if (fact.scope.source && sourceIds.contains(fact.scope.source->value) && fact.asset.valid()) {
      assetIds.insert(fact.asset.value);
    }
  }
  return assetIds;
}

void MatchFactStore::removeForSourcesOrAssets(const std::unordered_set<u32>& sourceIds,
                                              const std::unordered_set<u32>& assetIds) {
  std::erase_if(facts_, [&](const MatchFact& fact) {
    return (fact.scope.source && sourceIds.contains(fact.scope.source->value)) ||
           (fact.asset.valid() && assetIds.contains(fact.asset.value));
  });
}

void CollectionStore::clear() {
  collections_.clear();
  collectionsByKey_.clear();
}

void CollectionStore::reconcile(std::vector<DesiredCollection> desiredCollections,
                                const std::set<std::string>& activeResolvers, ScanIdAllocator& ids) {
  std::set<std::string> seenKeys;
  for (const auto& desired : desiredCollections) {
    if (desired.key.resolver.empty() || desired.key.value.empty()) {
      continue;
    }

    const auto key = collectionKeyString(desired.key);
    seenKeys.insert(key);

    if (const auto found = collectionsByKey_.find(key); found != collectionsByKey_.end()) {
      auto& collection = collections_[found->second];
      if (collection.origin == CollectionOrigin::UserCreated) {
        continue;
      }

      collection.name = desired.name;
      collection.origin = desired.origin;
      collection.status = desired.status;
      collection.sequence = desired.sequence;
      collection.instrumentSets = desired.instrumentSets;
      collection.sampleCollections = desired.sampleCollections;
      collection.miscAssets = desired.miscAssets;
      continue;
    }

    Collection collection{
        .id = ids.nextCollectionId(),
        .name = desired.name,
        .origin = desired.origin,
        .status = desired.status,
        .key = desired.key,
        .sequence = desired.sequence,
        .instrumentSets = desired.instrumentSets,
        .sampleCollections = desired.sampleCollections,
        .miscAssets = desired.miscAssets,
    };
    collectionsByKey_.emplace(key, collections_.size());
    collections_.push_back(std::move(collection));
  }

  for (auto& collection : collections_) {
    if (collection.origin != CollectionOrigin::Discovered || collection.key.resolver.empty()) {
      continue;
    }
    if (!activeResolvers.contains(collection.key.resolver)) {
      continue;
    }
    if (!seenKeys.contains(collectionKeyString(collection.key))) {
      collection.status = CollectionStatus::Stale;
    }
  }
}

void CollectionStore::markReferencesStale(const std::unordered_set<u32>& assetIds) {
  for (auto& collection : collections_) {
    if (referencesAsset(collection, assetIds)) {
      collection.status = CollectionStatus::Stale;
    }
  }
}

void CollectionStore::rebuildIndex() {
  collectionsByKey_.clear();
  for (size_t i = 0; i < collections_.size(); ++i) {
    if (!collections_[i].key.resolver.empty() && !collections_[i].key.value.empty()) {
      collectionsByKey_.emplace(collectionKeyString(collections_[i].key), i);
    }
  }
}

void DiagnosticStore::clear() {
  diagnostics_.clear();
}

void DiagnosticStore::add(std::vector<Diagnostic> diagnostics) {
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(diagnostics.begin()),
                      std::make_move_iterator(diagnostics.end()));
}

void DiagnosticStore::add(Diagnostic diagnostic) {
  diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticStore::removeForSources(const std::unordered_set<u32>& sourceIds) {
  std::erase_if(diagnostics_, [&](const Diagnostic& diagnostic) {
    return diagnostic.range && sourceIds.contains(diagnostic.range->source.value);
  });
}

}  // namespace vgmtrans::core
