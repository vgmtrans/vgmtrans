/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace vgmtrans::core {

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

SessionSnapshot::Storage::Storage(std::vector<SourceFile> sourcesValue, SharedSequence<Asset> assetsValue,
                                  std::vector<Collection> collectionsValue, SourceMap sourceMapValue,
                                  std::vector<Diagnostic> diagnosticsValue)
    : sources(std::move(sourcesValue)), assets(std::move(assetsValue)), collections(std::move(collectionsValue)),
      sourceMap(std::move(sourceMapValue)), diagnostics(std::move(diagnosticsValue)) {
  sourcesById.reserve(sources.size());
  for (const auto& source : sources) {
    if (source.id.valid()) {
      sourcesById.emplace(source.id, &source);
    }
  }

  assetsById.reserve(assets.size());
  for (const auto& asset : assets) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      assetsById.emplace(id, &asset);
    }
  }

  collectionsById.reserve(collections.size());
  for (const auto& collection : collections) {
    if (collection.id.valid()) {
      collectionsById.emplace(collection.id, &collection);
    }
  }
}

SessionSnapshot::SessionSnapshot(std::vector<SourceFile> sources, SharedSequence<Asset> assets,
                                 std::vector<Collection> collections, SourceMap sourceMap,
                                 std::vector<Diagnostic> diagnostics)
    : storage_(std::make_shared<const Storage>(std::move(sources), std::move(assets), std::move(collections),
                                               std::move(sourceMap), std::move(diagnostics))) {
}

const SourceFile* SessionSnapshot::source(SourceId id) const {
  const auto found = storage_->sourcesById.find(id);
  return found != storage_->sourcesById.end() ? found->second : nullptr;
}

const Asset* SessionSnapshot::asset(AssetId id) const {
  const auto found = storage_->assetsById.find(id);
  return found != storage_->assetsById.end() ? found->second : nullptr;
}

const Collection* SessionSnapshot::collection(CollectionId id) const {
  const auto found = storage_->collectionsById.find(id);
  return found != storage_->collectionsById.end() ? found->second : nullptr;
}

const Collection* SessionSnapshot::firstCollectionContaining(AssetId asset) const {
  for (const auto& collection : storage_->collections) {
    if (collection.contains(asset)) {
      return &collection;
    }
  }
  return nullptr;
}

size_t SessionSnapshot::countCollectionsContaining(AssetId asset) const {
  return std::ranges::count_if(storage_->collections,
                               [asset](const Collection& collection) { return collection.contains(asset); });
}

}  // namespace vgmtrans::core
