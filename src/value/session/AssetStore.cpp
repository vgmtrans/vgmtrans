/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/AssetStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::core {

bool AssetStore::contains(AssetId id) const noexcept {
  return id.valid() && assetsById_.contains(id.value);
}

const Asset* AssetStore::find(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }

  const auto found = assetsById_.find(id.value);
  if (found == assetsById_.end() || found->second >= assets_.size()) {
    return nullptr;
  }
  return &assets_[found->second];
}

void AssetStore::append(std::vector<Asset> assets, SourceId owner) {
  std::unordered_set<u32> batchIds;
  for (const auto& asset : assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      throw std::invalid_argument("Scan result contained an asset without an id");
    }
    if (!batchIds.insert(id.value).second) {
      throw std::invalid_argument("Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (assetsById_.contains(id.value)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (auto& asset : assets) {
    const auto id = metadata(asset).id;
    sourceOwners_.emplace(id.value, owner);
    assetsById_.emplace(id.value, assets_.size());
    assets_.push_back(std::move(asset));
  }
}

std::string AssetStore::materializedKey(std::string_view resolverId, const CollectionKey& collection,
                                        std::string_view slot) const {
  return std::string(resolverId) + '\x1f' + collection.resolver + '\x1f' + collection.value + '\x1f' +
         std::string(slot);
}

AssetId AssetStore::materializedAssetId(std::string_view resolverId, const CollectionKey& collection,
                                        std::string_view slot, ScanIdAllocator& ids) {
  const auto key = materializedKey(resolverId, collection, slot);
  if (auto found = materializedAssets_.find(key); found != materializedAssets_.end()) {
    return found->second.id;
  }

  AssetId id;
  do {
    id = ids.nextAssetId();
  } while (contains(id));
  materializedAssets_[key] = MaterializedAssetRecord{
      .id = id,
      .resolver = std::string(resolverId),
  };
  return id;
}

std::string AssetStore::upsertMaterializedAsset(std::string_view resolverId, const CollectionKey& collection,
                                                std::string_view slot, Asset asset) {
  const auto key = materializedKey(resolverId, collection, slot);
  const AssetId id = metadata(asset).id;
  if (!id.valid()) {
    throw std::invalid_argument("Materialized asset '" + key + "' did not receive an asset id");
  }

  auto known = materializedAssets_.find(key);
  if (known == materializedAssets_.end()) {
    if (contains(id)) {
      throw std::invalid_argument("Materialized asset '" + key + "' reused existing asset id " +
                                  std::to_string(id.value));
    }
    materializedAssets_[key] = MaterializedAssetRecord{
        .id = id,
        .resolver = std::string(resolverId),
    };
  } else if (known->second.id != id) {
    throw std::invalid_argument("Materialized asset '" + key + "' used unstable asset id " +
                                std::to_string(id.value));
  }

  materializedKeysById_[id.value] = key;
  if (auto found = assetsById_.find(id.value); found != assetsById_.end()) {
    assets_[found->second] = std::move(asset);
  } else {
    assetsById_.emplace(id.value, assets_.size());
    assets_.push_back(std::move(asset));
  }
  return key;
}

std::unordered_set<u32> AssetStore::removeStaleMaterializedAssets(std::string_view resolverId,
                                                                 const std::set<std::string>& activeKeys) {
  std::vector<std::string> staleKeys;
  std::unordered_set<u32> removedAssetIds;
  for (const auto& [key, record] : materializedAssets_) {
    if (record.resolver == resolverId && !activeKeys.contains(key)) {
      staleKeys.push_back(key);
    }
  }

  if (staleKeys.empty()) {
    return removedAssetIds;
  }

  for (const auto& key : staleKeys) {
    if (auto found = materializedAssets_.find(key); found != materializedAssets_.end()) {
      removedAssetIds.insert(found->second.id.value);
      materializedAssets_.erase(found);
    }
  }

  std::erase_if(assets_, [&](const Asset& asset) {
    const AssetId id = metadata(asset).id;
    return id.valid() && removedAssetIds.contains(id.value);
  });
  for (const u32 id : removedAssetIds) {
    materializedKeysById_.erase(id);
  }
  rebuildIndex();
  return removedAssetIds;
}

std::unordered_set<u32> AssetStore::removeForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = makeSourceIdSet(sources);
  std::unordered_set<u32> removedAssetIds;

  for (const auto& [assetId, sourceId] : sourceOwners_) {
    if (sourceIds.contains(sourceId)) {
      removedAssetIds.insert(assetId);
    }
  }

  for (const auto& asset : assets_) {
    const auto& meta = metadata(asset);
    if (meta.range.valid() && sourceIds.contains(meta.range.source) && meta.id.valid()) {
      removedAssetIds.insert(meta.id.value);
    }
  }

  std::erase_if(assets_, [&](const Asset& asset) {
    const auto id = metadata(asset).id;
    return id.valid() && removedAssetIds.contains(id.value);
  });
  for (const u32 id : removedAssetIds) {
    sourceOwners_.erase(id);
    if (auto key = materializedKeysById_.find(id); key != materializedKeysById_.end()) {
      materializedAssets_.erase(key->second);
      materializedKeysById_.erase(key);
    }
  }
  rebuildIndex();

  return removedAssetIds;
}

// Removing assets compacts the vector, so every stored vector index must be
// rebuilt before the next ID lookup.
void AssetStore::rebuildIndex() {
  assetsById_.clear();
  assetsById_.reserve(assets_.size());
  for (size_t index = 0; index < assets_.size(); ++index) {
    const auto id = metadata(assets_[index]).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, index);
    }
  }
}

}  // namespace vgmtrans::core
