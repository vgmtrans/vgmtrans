/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/AssetStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <stdexcept>
#include <string>
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
