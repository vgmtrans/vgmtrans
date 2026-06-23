/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/MatchModel.h"
#include "value/model/SourceMap.h"
#include "value/sequence/SequenceProgram.h"
#include "value/synth/SynthModel.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// Copyable view of the current Session state. UI, tests, and export read this
// snapshot; Session owns the mutable stores.

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
};

using Asset = std::variant<SequenceProgramAsset, InstrumentSetAsset, SampleCollectionAsset, MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  CollectionStatus status = CollectionStatus::Complete;
  CollectionOrigin origin = CollectionOrigin::Discovered;
  CollectionKey key;
  // Collections are the export units. A sequence may be paired with instrument
  // sets and sample collections loaded from the same or separate sources.
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
  std::vector<CollectionIssue> issues;
};

class SessionSnapshotBuilder;

// Copyable read-only view of the current Session state. UI, tests, and export
// read this snapshot; Session owns the mutable stores.
class SessionSnapshot {
public:
  [[nodiscard]] const std::vector<SourceFile>& sources() const noexcept { return sources_; }
  [[nodiscard]] const std::vector<Asset>& assets() const noexcept { return assets_; }
  [[nodiscard]] const std::vector<MatchFact>& matchFacts() const noexcept { return matchFacts_; }
  [[nodiscard]] const std::vector<Collection>& collections() const noexcept { return collections_; }
  [[nodiscard]] const SourceMap& sourceMap() const noexcept { return sourceMap_; }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

  [[nodiscard]] const Asset* asset(AssetId id) const;

  template <typename T>
  [[nodiscard]] const T* asset(AssetId id) const {
    const auto* found = asset(id);
    return found != nullptr ? std::get_if<T>(found) : nullptr;
  }

  [[nodiscard]] const Collection* collection(CollectionId id) const;

private:
  friend class SessionSnapshotBuilder;

  struct Index {
    // Store vector indexes rather than pointers so snapshots stay easy to copy/move.
    std::unordered_map<u32, size_t> assetsById;
    std::unordered_map<u32, size_t> collectionsById;
  };

  SessionSnapshot(std::vector<SourceFile> sources, std::vector<Asset> assets, std::vector<MatchFact> matchFacts,
                  std::vector<Collection> collections, SourceMap sourceMap, std::vector<Diagnostic> diagnostics);

  [[nodiscard]] static Index buildIndex(const std::vector<Asset>& assets, const std::vector<Collection>& collections);

  std::vector<SourceFile> sources_;
  std::vector<Asset> assets_;
  std::vector<MatchFact> matchFacts_;
  std::vector<Collection> collections_;
  SourceMap sourceMap_;
  std::vector<Diagnostic> diagnostics_;
  Index index_;
};

class SessionSnapshotBuilder {
public:
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<Collection> collections;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] SessionSnapshot finish();
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
  // Non-owning pointers to the assets in one Collection. Exporters use this
  // instead of repeatedly resolving IDs.
  const Collection* collection = nullptr;
  const SequenceProgramAsset* sequenceProgram = nullptr;
  std::vector<const InstrumentSetAsset*> instrumentSets;
  std::vector<const SampleCollectionAsset*> sampleCollections;
  std::vector<const MiscAsset*> miscAssets;
  CollectionAssetDiagnostics diagnostics;
};

[[nodiscard]] AssetMetadata& metadata(Asset& asset);
[[nodiscard]] const AssetMetadata& metadata(const Asset& asset);
[[nodiscard]] const Asset* assetById(const SessionSnapshot& snapshot, AssetId id);

template <typename T>
[[nodiscard]] const T* assetById(const SessionSnapshot& snapshot, AssetId id) {
  return snapshot.asset<T>(id);
}

[[nodiscard]] const Collection* collectionById(const SessionSnapshot& snapshot, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, CollectionId id);
[[nodiscard]] CollectionAssets resolveCollectionAssets(const SessionSnapshot& snapshot, const Collection& collection);

}  // namespace vgmtrans::core
