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

SessionSnapshot::SessionSnapshot(std::vector<SourceFile> sources, std::vector<Asset> assets,
                                 std::vector<MatchFact> matchFacts, std::vector<Collection> collections,
                                 SourceMap sourceMap, std::vector<Diagnostic> diagnostics)
    : sources_(std::move(sources)), assets_(std::move(assets)), matchFacts_(std::move(matchFacts)),
      collections_(std::move(collections)), sourceMap_(std::move(sourceMap)), diagnostics_(std::move(diagnostics)),
      index_(buildIndex(sources_, assets_, collections_)) {
}

SessionSnapshot::Index SessionSnapshot::buildIndex(const std::vector<SourceFile>& sources,
                                                   const std::vector<Asset>& assets,
                                                   const std::vector<Collection>& collections) {
  Index index;
  index.sourcesById.reserve(sources.size());
  for (size_t i = 0; i < sources.size(); ++i) {
    const SourceId id = sources[i].id;
    if (id.valid()) {
      index.sourcesById.emplace(id.value, i);
    }
  }

  index.assetsById.reserve(assets.size());
  for (size_t i = 0; i < assets.size(); ++i) {
    const AssetId id = metadata(assets[i]).id;
    if (id.valid()) {
      index.assetsById.emplace(id.value, i);
    }
  }

  index.collectionsById.reserve(collections.size());
  for (size_t i = 0; i < collections.size(); ++i) {
    const CollectionId id = collections[i].id;
    if (id.valid()) {
      index.collectionsById.emplace(id.value, i);
    }
  }

  return index;
}

const SourceFile* SessionSnapshot::source(SourceId id) const {
  const auto found = index_.sourcesById.find(id.value);
  if (found == index_.sourcesById.end() || found->second >= sources_.size()) {
    return nullptr;
  }
  return &sources_[found->second];
}

const Asset* SessionSnapshot::asset(AssetId id) const {
  const auto found = index_.assetsById.find(id.value);
  if (found == index_.assetsById.end() || found->second >= assets_.size()) {
    return nullptr;
  }
  return &assets_[found->second];
}

const Collection* SessionSnapshot::collection(CollectionId id) const {
  const auto found = index_.collectionsById.find(id.value);
  if (found == index_.collectionsById.end() || found->second >= collections_.size()) {
    return nullptr;
  }
  return &collections_[found->second];
}

const Collection* SessionSnapshot::firstCollectionContaining(AssetId asset) const {
  for (const auto& collection : collections_) {
    if (collection.sequence == asset ||
        std::ranges::find(collection.instrumentSets, asset) != collection.instrumentSets.end() ||
        std::ranges::find(collection.sampleCollections, asset) != collection.sampleCollections.end() ||
        std::ranges::find(collection.miscAssets, asset) != collection.miscAssets.end()) {
      return &collection;
    }
  }
  return nullptr;
}

}  // namespace vgmtrans::core
