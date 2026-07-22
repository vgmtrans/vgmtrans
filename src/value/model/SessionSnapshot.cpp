/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SessionSnapshot.h"

#include "value/validation/SnapshotValidation.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] Diagnostic snapshotError(std::string message) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
  };
}

}  // namespace

std::vector<Diagnostic> CollectionAssetDiagnostics::all() const {
  std::vector<Diagnostic> diagnostics;
  diagnostics.reserve(collection.size() + sequence.size() + instrumentSets.size() + sampleCollections.size() +
                      miscAssets.size());
  diagnostics.insert(diagnostics.end(), collection.begin(), collection.end());
  diagnostics.insert(diagnostics.end(), sequence.begin(), sequence.end());
  diagnostics.insert(diagnostics.end(), instrumentSets.begin(), instrumentSets.end());
  diagnostics.insert(diagnostics.end(), sampleCollections.begin(), sampleCollections.end());
  diagnostics.insert(diagnostics.end(), miscAssets.begin(), miscAssets.end());
  return diagnostics;
}

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

SessionSnapshot SessionSnapshotBuilder::finish() {
  auto indexDiagnostics = validateSessionSnapshotState(assets, collections).diagnostics();
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(indexDiagnostics.begin()),
                     std::make_move_iterator(indexDiagnostics.end()));
  return SessionSnapshot{
      std::move(sources),     std::move(assets),    std::move(matchFacts),
      std::move(collections), std::move(sourceMap), std::move(diagnostics),
  };
}

const Asset* assetById(const SessionSnapshot& snapshot, AssetId id) {
  return snapshot.asset(id);
}

const Collection* collectionById(const SessionSnapshot& snapshot, CollectionId id) {
  return snapshot.collection(id);
}

CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, CollectionId id) {
  if (const auto* collection = collectionById(snapshot, id)) {
    return resolveCollectionAssets(snapshot, *collection);
  }

  CollectionAssets resolved;
  resolved.diagnostics.collection.push_back(snapshotError("CollectionId was not found in the SessionSnapshot"));
  return resolved;
}

CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, const Collection& collection) {
  // Resolve collection references once before export. The returned pointers are the usable
  // assets; diagnostics describe any missing or wrong-type references.
  CollectionAssets resolved{
      .collection = &collection,
  };

  if (collection.sequence) {
    if (const auto* sequenceProgram = assetById<SequenceProgramAsset>(snapshot, *collection.sequence)) {
      resolved.sequenceProgram = sequenceProgram;
    } else {
      resolved.diagnostics.sequence.push_back(snapshotError("Collection sequence asset was not found"));
    }
  }

  resolved.instrumentSets.reserve(collection.instrumentSets.size());
  for (const auto id : collection.instrumentSets) {
    if (const auto* instrumentSet = assetById<InstrumentSetAsset>(snapshot, id)) {
      resolved.instrumentSets.push_back(instrumentSet);
    } else {
      resolved.diagnostics.instrumentSets.push_back(snapshotError("Collection instrument set asset was not found"));
    }
  }

  resolved.sampleCollections.reserve(collection.sampleCollections.size());
  for (const auto id : collection.sampleCollections) {
    if (const auto* sampleCollection = assetById<SampleCollectionAsset>(snapshot, id)) {
      resolved.sampleCollections.push_back(sampleCollection);
    } else {
      resolved.diagnostics.sampleCollections.push_back(
          snapshotError("Collection sample collection asset was not found"));
    }
  }

  resolved.miscAssets.reserve(collection.miscAssets.size());
  for (const auto id : collection.miscAssets) {
    if (const auto* misc = assetById<MiscAsset>(snapshot, id)) {
      resolved.miscAssets.push_back(misc);
    } else {
      resolved.diagnostics.miscAssets.push_back(snapshotError("Collection misc asset was not found"));
    }
  }

  return resolved;
}

}  // namespace vgmtrans::core
