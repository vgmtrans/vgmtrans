/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/AssetStore.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::unordered_set<u32> sourceIdSet(const std::vector<SourceId>& sources) {
  std::unordered_set<u32> ids;
  ids.reserve(sources.size());
  for (const SourceId source : sources) {
    ids.insert(source.value);
  }
  return ids;
}

}  // namespace

bool AssetStore::contains(AssetId id) const noexcept {
  return id.valid() && sourceOwners_.contains(id.value);
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
    if (sourceOwners_.contains(id.value)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (auto& asset : assets) {
    const auto id = metadata(asset).id;
    sourceOwners_.emplace(id.value, owner.value);
    assets_.push_back(std::move(asset));
  }
}

std::unordered_set<u32> AssetStore::removeForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = sourceIdSet(sources);
  std::unordered_set<u32> removedAssetIds;

  for (const auto& [assetId, sourceId] : sourceOwners_) {
    if (sourceIds.contains(sourceId)) {
      removedAssetIds.insert(assetId);
    }
  }

  for (const auto& asset : assets_) {
    const auto& meta = metadata(asset);
    if (meta.range.valid() && sourceIds.contains(meta.range.source.value) && meta.id.valid()) {
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

  return removedAssetIds;
}

}  // namespace vgmtrans::core
