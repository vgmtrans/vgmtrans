/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/ExplicitCollectionStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool referencesAnyAsset(const ExplicitCollection& collection, const std::unordered_set<u32>& assetIds) {
  const auto referencesOne = [&](std::optional<AssetId> id) { return id && assetIds.contains(id->value); };
  const auto referencesMany = [&](const std::vector<AssetId>& ids) {
    return std::ranges::any_of(ids, [&](AssetId id) { return assetIds.contains(id.value); });
  };
  return referencesOne(collection.sequence) || referencesMany(collection.instrumentSets) ||
         referencesMany(collection.sampleCollections) || referencesMany(collection.miscAssets);
}

[[nodiscard]] DesiredCollection toDesiredCollection(const ExplicitCollection& collection) {
  return DesiredCollection{
      .key = collection.key,
      .name = collection.name,
      .sequence = collection.sequence,
      .instrumentSets = collection.instrumentSets,
      .sampleCollections = collection.sampleCollections,
      .miscAssets = collection.miscAssets,
  };
}

}  // namespace

void ExplicitCollectionStore::append(std::vector<ExplicitCollection> collections, SourceId owner) {
  for (auto& collection : collections) {
    if (!collection.key.resolver.empty()) {
      knownResolvers_.insert(collection.key.resolver);
    }
    entries_.push_back(Entry{
        .owner = owner,
        .collection = std::move(collection),
    });
  }
}

void ExplicitCollectionStore::removeForSourcesAndAssets(const std::vector<SourceId>& sources,
                                                        const std::unordered_set<u32>& assetIds) {
  const auto sourceIds = makeSourceIdSet(sources);
  std::erase_if(entries_, [&](const Entry& entry) {
    return sourceIds.contains(entry.owner) || referencesAnyAsset(entry.collection, assetIds);
  });
}

std::map<std::string, std::vector<DesiredCollection>> ExplicitCollectionStore::desiredByResolver() const {
  std::map<std::string, std::vector<DesiredCollection>> grouped;
  for (const auto& resolver : knownResolvers_) {
    grouped.try_emplace(resolver);
  }

  for (const auto& entry : entries_) {
    if (entry.collection.key.resolver.empty()) {
      continue;
    }
    grouped[entry.collection.key.resolver].push_back(toDesiredCollection(entry.collection));
  }
  return grouped;
}

}  // namespace vgmtrans::core
