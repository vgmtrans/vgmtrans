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

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
};

using Asset = std::variant<SequenceAsset, InstrumentSetAsset, SampleCollectionAsset, MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
};

struct Project {
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
