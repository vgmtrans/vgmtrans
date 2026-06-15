/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/MatchModel.h"
#include "value/sequence/SequenceProgram.h"
#include "value/synth/SynthModel.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// SessionSnapshot is a copyable view of the current Session stores. It is the
// read model for UI, tests, and exports; Session remains the mutable owner.

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
};

using Asset = std::variant<SequenceProgramAsset, InstrumentSetAsset, SampleCollectionAsset, MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  CollectionOrigin origin = CollectionOrigin::Discovered;
  CollectionStatus status = CollectionStatus::Complete;
  CollectionKey key;
  // Collections are the export units. A sequence can be paired with zero or more synth
  // and sample assets because some games store those banks separately or share them.
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
};

struct SessionSnapshotIndex {
  // Index entries are vector indices rather than pointers so snapshots remain
  // normal movable/copyable values.
  bool valid = false;
  std::unordered_map<u32, size_t> assetsById;
  std::unordered_map<u32, size_t> collectionsById;
};

struct SessionSnapshot {
  // Sources includes user-added files plus extracted derived files such as SPC RAM or
  // archive members. Asset ranges refer back into this list.
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<Collection> collections;
  std::vector<Diagnostic> diagnostics;
  SessionSnapshotIndex index;
};

struct CollectionAssetDiagnostics {
  std::vector<Diagnostic> collection;
  std::vector<Diagnostic> sequence;
  std::vector<Diagnostic> instrumentSets;
  std::vector<Diagnostic> sampleCollections;
  std::vector<Diagnostic> miscAssets;

  [[nodiscard]] std::vector<Diagnostic> all() const;
};

struct CollectionAssets {
  // Resolved pointer view over a SessionSnapshot. It is intentionally non-owning
  // so exporters can work without copying large sample/instrument assets.
  const Collection* collection = nullptr;
  const SequenceProgramAsset* sequenceProgram = nullptr;
  std::vector<const InstrumentSetAsset*> instrumentSets;
  std::vector<const SampleCollectionAsset*> sampleCollections;
  std::vector<const MiscAsset*> miscAssets;
  CollectionAssetDiagnostics diagnostics;
};

[[nodiscard]] AssetMetadata& metadata(Asset& asset);
[[nodiscard]] const AssetMetadata& metadata(const Asset& asset);
[[nodiscard]] SessionSnapshotIndex buildSessionSnapshotIndex(const SessionSnapshot& snapshot);
[[nodiscard]] std::vector<Diagnostic> sessionSnapshotIndexDiagnostics(const SessionSnapshot& snapshot);
void rebuildSessionSnapshotIndex(SessionSnapshot& snapshot);
void finalizeSessionSnapshotIndex(SessionSnapshot& snapshot);
[[nodiscard]] ItemNode* itemById(ItemTree& tree, ItemId id);
[[nodiscard]] const ItemNode* itemById(const ItemTree& tree, ItemId id);
[[nodiscard]] Asset* assetById(SessionSnapshot& snapshot, AssetId id);
[[nodiscard]] const Asset* assetById(const SessionSnapshot& snapshot, AssetId id);

template <typename T>
[[nodiscard]] const T* assetById(const SessionSnapshot& snapshot, AssetId id) {
  const auto* asset = assetById(snapshot, id);
  if (asset == nullptr) {
    return nullptr;
  }
  return std::get_if<T>(asset);
}

[[nodiscard]] const Collection* collectionById(const SessionSnapshot& snapshot, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, const Collection& collection);

}  // namespace vgmtrans::core
