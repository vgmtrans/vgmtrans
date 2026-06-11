/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceModel.h"
#include "value/core/Source.h"
#include "value/core/SynthModel.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// ProjectModel is a snapshot of everything discovered from the current SourceStore.
// It deliberately stores assets by value and references them by stable IDs so scans,
// UI inspection, and exports can share one immutable-looking result.

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
};

using Asset = std::variant<SequenceAsset, InstrumentSetAsset, SampleCollectionAsset, MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  // Collections are the export units. A sequence can be paired with zero or more synth
  // and sample assets because some games store those banks separately or share them.
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
};

struct Project {
  // Sources includes user-added files plus extracted virtual files such as SPC RAM or
  // archive members. Asset ranges refer back into this list.
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<Collection> collections;
  std::vector<Diagnostic> diagnostics;
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
  // Resolved pointer view over a Project. It is intentionally non-owning so exporters can
  // work without copying large sample/instrument assets.
  const Collection* collection = nullptr;
  const SequenceAsset* sequence = nullptr;
  std::vector<const InstrumentSetAsset*> instrumentSets;
  std::vector<const SampleCollectionAsset*> sampleCollections;
  std::vector<const MiscAsset*> miscAssets;
  CollectionAssetDiagnostics diagnostics;
};

[[nodiscard]] AssetMetadata& metadata(Asset& asset);
[[nodiscard]] const AssetMetadata& metadata(const Asset& asset);
[[nodiscard]] ItemNode* itemById(ItemTree& tree, ItemId id);
[[nodiscard]] const ItemNode* itemById(const ItemTree& tree, ItemId id);
[[nodiscard]] Asset* assetById(Project& project, AssetId id);
[[nodiscard]] const Asset* assetById(const Project& project, AssetId id);

template <typename T>
[[nodiscard]] const T* assetById(const Project& project, AssetId id) {
  const auto* asset = assetById(project, id);
  if (asset == nullptr) {
    return nullptr;
  }
  return std::get_if<T>(asset);
}

[[nodiscard]] const Collection* collectionById(const Project& project, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const Project& project, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const Project& project, const Collection& collection);

}  // namespace vgmtrans::core
