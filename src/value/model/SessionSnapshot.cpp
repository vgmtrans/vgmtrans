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
                                  SharedSequence<MatchFact> matchFactsValue, std::vector<Collection> collectionsValue,
                                  SourceMap sourceMapValue, std::vector<Diagnostic> diagnosticsValue)
    : sources(std::move(sourcesValue)), assets(std::move(assetsValue)), matchFacts(std::move(matchFactsValue)),
      collections(std::move(collectionsValue)), sourceMap(std::move(sourceMapValue)),
      diagnostics(std::move(diagnosticsValue)), index(buildIndex(sources, assets, collections)) {
}

SessionSnapshot::SessionSnapshot(std::vector<SourceFile> sources, SharedSequence<Asset> assets,
                                 SharedSequence<MatchFact> matchFacts, std::vector<Collection> collections,
                                 SourceMap sourceMap, std::vector<Diagnostic> diagnostics)
    : storage_(std::make_shared<const Storage>(std::move(sources), std::move(assets), std::move(matchFacts),
                                               std::move(collections), std::move(sourceMap), std::move(diagnostics))) {
}

SessionSnapshot::Index SessionSnapshot::buildIndex(const std::vector<SourceFile>& sources,
                                                   const SharedSequence<Asset>& assets,
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
  for (const auto& asset : assets) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      index.assetsById.emplace(id.value, &asset);
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
  const auto found = storage_->index.sourcesById.find(id.value);
  return found != storage_->index.sourcesById.end() ? &storage_->sources[found->second] : nullptr;
}

const Asset* SessionSnapshot::asset(AssetId id) const {
  const auto found = storage_->index.assetsById.find(id.value);
  return found != storage_->index.assetsById.end() ? found->second : nullptr;
}

const Collection* SessionSnapshot::collection(CollectionId id) const {
  const auto found = storage_->index.collectionsById.find(id.value);
  return found != storage_->index.collectionsById.end() ? &storage_->collections[found->second] : nullptr;
}

const Collection* SessionSnapshot::firstCollectionContaining(AssetId asset) const {
  for (const auto& collection : storage_->collections) {
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
