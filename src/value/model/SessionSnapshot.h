/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/CollectionModel.h"
#include "value/model/SharedSequence.h"
#include "value/model/SourceMap.h"
#include "value/sequence/SequenceProgram.h"
#include "value/synth/SynthModel.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

namespace detail {
class SessionSnapshotAccess;
}

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
  AssetPrivateData privateData;
};

using Asset = std::variant<SequenceProgramAsset, SoundBankAsset, SamplePoolAsset, MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  CollectionFreshness freshness = CollectionFreshness::Current;
  CollectionOrigin origin = CollectionOrigin::Discovered;
  CollectionKey key;
  // Chosen during session resolution so binding never has to recover format
  // behavior from a registry.
  CollectionBinder binder;
  // Collections are the export units. A sequence may be paired with instrument
  // banks and sample pools loaded from the same or separate sources.
  CollectionMembers members;
  std::vector<CollectionIssue> issues;

  [[nodiscard]] CollectionResolution resolution() const noexcept { return collectionResolution(issues); }
};

// Copyable read-only view of one Session revision. Copies share immutable
// storage, so a snapshot remains stable across later Session mutations without
// duplicating the discovered value graph. Scan-owned domains remain ordered
// logical sequences even when their backing is shared in multiple chunks.
class SessionSnapshot {
public:
  [[nodiscard]] const std::vector<SourceFile>& sources() const noexcept { return storage_->sources; }
  [[nodiscard]] const SharedSequence<Asset>& assets() const noexcept { return storage_->assets; }
  [[nodiscard]] const std::vector<Collection>& collections() const noexcept { return storage_->collections; }
  [[nodiscard]] const SourceMap& sourceMap() const noexcept { return storage_->sourceMap; }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return storage_->diagnostics; }

  [[nodiscard]] const SourceFile* source(SourceId id) const;
  [[nodiscard]] const Asset* asset(AssetId id) const;

  template <typename T>
  [[nodiscard]] const T* asset(AssetId id) const {
    const auto* found = asset(id);
    return found != nullptr ? std::get_if<T>(found) : nullptr;
  }

  [[nodiscard]] const Collection* collection(CollectionId id) const;
  [[nodiscard]] const Collection* firstCollectionContaining(AssetId asset) const;
  [[nodiscard]] size_t countCollectionsContaining(AssetId asset) const;

private:
  friend class detail::SessionSnapshotAccess;

  struct Index {
    std::unordered_map<u32, size_t> sourcesById;
    std::unordered_map<u32, const Asset*> assetsById;
    std::unordered_map<u32, size_t> collectionsById;
  };

  struct Storage {
    Storage(std::vector<SourceFile> sources, SharedSequence<Asset> assets, std::vector<Collection> collections,
            SourceMap sourceMap, std::vector<Diagnostic> diagnostics);

    std::vector<SourceFile> sources;
    SharedSequence<Asset> assets;
    std::vector<Collection> collections;
    SourceMap sourceMap;
    std::vector<Diagnostic> diagnostics;
    Index index;
  };

  SessionSnapshot(std::vector<SourceFile> sources, SharedSequence<Asset> assets, std::vector<Collection> collections,
                  SourceMap sourceMap, std::vector<Diagnostic> diagnostics);

  [[nodiscard]] static Index buildIndex(const std::vector<SourceFile>& sources, const SharedSequence<Asset>& assets,
                                        const std::vector<Collection>& collections);

  std::shared_ptr<const Storage> storage_;
};

[[nodiscard]] AssetMetadata& metadata(Asset& asset);
[[nodiscard]] const AssetMetadata& metadata(const Asset& asset);

}  // namespace vgmtrans::core
