/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_set>
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

[[nodiscard]] Diagnostic snapshotError(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
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

SessionSnapshotIndex buildSessionSnapshotIndex(const SessionSnapshot& snapshot) {
  SessionSnapshotIndex index;
  index.assetsById.reserve(snapshot.assets.size());
  for (size_t i = 0; i < snapshot.assets.size(); ++i) {
    const AssetId id = metadata(snapshot.assets[i]).id;
    if (id.valid()) {
      index.assetsById.emplace(id.value, i);
    }
  }

  index.collectionsById.reserve(snapshot.collections.size());
  for (size_t i = 0; i < snapshot.collections.size(); ++i) {
    const CollectionId id = snapshot.collections[i].id;
    if (id.valid()) {
      index.collectionsById.emplace(id.value, i);
    }
  }

  index.valid = true;
  return index;
}

std::vector<Diagnostic> sessionSnapshotIndexDiagnostics(const SessionSnapshot& snapshot) {
  std::vector<Diagnostic> diagnostics;

  std::unordered_set<u32> assetIds;
  assetIds.reserve(snapshot.assets.size());
  for (const auto& asset : snapshot.assets) {
    const auto& meta = metadata(asset);
    if (!meta.id.valid()) {
      continue;
    }

    if (assetIds.insert(meta.id.value).second) {
      continue;
    }

    const std::string message = "Duplicate asset id " + std::to_string(meta.id.value) + " in SessionSnapshot";
    if (meta.range.valid()) {
      diagnostics.push_back(snapshotError(message, meta.range));
    } else {
      diagnostics.push_back(snapshotError(message));
    }
  }

  std::unordered_set<u32> collectionIds;
  collectionIds.reserve(snapshot.collections.size());
  for (const auto& collection : snapshot.collections) {
    if (!collection.id.valid()) {
      continue;
    }

    if (!collectionIds.insert(collection.id.value).second) {
      diagnostics.push_back(
          snapshotError("Duplicate collection id " + std::to_string(collection.id.value) + " in SessionSnapshot"));
    }
  }

  return diagnostics;
}

void rebuildSessionSnapshotIndex(SessionSnapshot& snapshot) {
  snapshot.index = buildSessionSnapshotIndex(snapshot);
}

void finalizeSessionSnapshotIndex(SessionSnapshot& snapshot) {
  rebuildSessionSnapshotIndex(snapshot);

  auto diagnostics = sessionSnapshotIndexDiagnostics(snapshot);
  snapshot.diagnostics.insert(snapshot.diagnostics.end(), std::make_move_iterator(diagnostics.begin()),
                              std::make_move_iterator(diagnostics.end()));
}

ItemNode* itemById(ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) { return item.id == id; });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

const ItemNode* itemById(const ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) { return item.id == id; });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

Asset* assetById(SessionSnapshot& snapshot, AssetId id) {
  if (snapshot.index.valid) {
    const auto found = snapshot.index.assetsById.find(id.value);
    if (found == snapshot.index.assetsById.end() || found->second >= snapshot.assets.size()) {
      return nullptr;
    }
    return &snapshot.assets[found->second];
  }

  const auto found =
      std::ranges::find_if(snapshot.assets, [id](const Asset& asset) { return metadata(asset).id == id; });
  if (found == snapshot.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Asset* assetById(const SessionSnapshot& snapshot, AssetId id) {
  if (snapshot.index.valid) {
    const auto found = snapshot.index.assetsById.find(id.value);
    if (found == snapshot.index.assetsById.end() || found->second >= snapshot.assets.size()) {
      return nullptr;
    }
    return &snapshot.assets[found->second];
  }

  const auto found =
      std::ranges::find_if(snapshot.assets, [id](const Asset& asset) { return metadata(asset).id == id; });
  if (found == snapshot.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Collection* collectionById(const SessionSnapshot& snapshot, CollectionId id) {
  if (snapshot.index.valid) {
    const auto found = snapshot.index.collectionsById.find(id.value);
    if (found == snapshot.index.collectionsById.end() || found->second >= snapshot.collections.size()) {
      return nullptr;
    }
    return &snapshot.collections[found->second];
  }

  const auto found =
      std::ranges::find_if(snapshot.collections, [id](const Collection& collection) { return collection.id == id; });
  if (found == snapshot.collections.end()) {
    return nullptr;
  }
  return &*found;
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
