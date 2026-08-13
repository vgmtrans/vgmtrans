/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SnapshotValidation.h"

#include <string>
#include <string_view>
#include <unordered_set>

namespace vgmtrans::core {

namespace {

enum class CollectionAssetRole {
  Sequence,
  InstrumentSet,
  SampleCollection,
  Misc,
};

[[nodiscard]] std::string_view roleName(CollectionAssetRole role) {
  switch (role) {
    case CollectionAssetRole::Sequence:
      return "sequence";
    case CollectionAssetRole::InstrumentSet:
      return "instrument-set";
    case CollectionAssetRole::SampleCollection:
      return "sample-collection";
    case CollectionAssetRole::Misc:
      return "misc";
  }

  return "asset";
}

[[nodiscard]] bool collectionAssetRefExists(const SessionSnapshot& snapshot, AssetId id, CollectionAssetRole role) {
  switch (role) {
    case CollectionAssetRole::Sequence:
      return snapshot.asset<SequenceProgramAsset>(id) != nullptr;
    case CollectionAssetRole::InstrumentSet:
      return snapshot.asset<InstrumentSetAsset>(id) != nullptr;
    case CollectionAssetRole::SampleCollection:
      return snapshot.asset<SampleCollectionAsset>(id) != nullptr;
    case CollectionAssetRole::Misc:
      return snapshot.asset<MiscAsset>(id) != nullptr;
  }

  return false;
}

void validateCollectionAssetRef(ValidationReport& report, const SessionSnapshot& snapshot, AssetId id,
                                CollectionAssetRole role) {
  const auto name = roleName(role);
  if (!id.valid()) {
    report.error("snapshot.collection.invalid-reference",
                 "Collection referenced an invalid " + std::string(name) + " asset id");
    return;
  }

  if (!collectionAssetRefExists(snapshot, id, role)) {
    report.error(
        "snapshot.collection.missing-reference",
        "Collection referenced missing or wrong-type " + std::string(name) + " asset id " + std::to_string(id.value));
  }
}

}  // namespace

ValidationReport validateSessionSnapshotState(std::span<const Asset> assets, std::span<const Collection> collections) {
  ValidationReport report;

  // Snapshot construction has vectors but no lookup index yet, so this only
  // checks invariants that do not require SessionSnapshot helper methods.
  std::unordered_set<u32> assetIds;
  assetIds.reserve(assets.size());
  for (const auto& asset : assets) {
    const auto& meta = metadata(asset);
    if (!meta.id.valid()) {
      continue;
    }

    if (!assetIds.insert(meta.id.value).second) {
      report.error("snapshot.asset.duplicate-id",
                   "Duplicate asset id " + std::to_string(meta.id.value) + " in SessionSnapshot",
                   meta.range.valid() ? std::optional<SourceRange>{meta.range} : std::nullopt);
    }
  }

  std::unordered_set<u32> collectionIds;
  collectionIds.reserve(collections.size());
  for (const auto& collection : collections) {
    if (!collection.id.valid()) {
      continue;
    }

    if (!collectionIds.insert(collection.id.value).second) {
      report.error("snapshot.collection.duplicate-id",
                   "Duplicate collection id " + std::to_string(collection.id.value) + " in SessionSnapshot");
    }
  }

  return report;
}

ValidationReport validateSessionSnapshot(const SessionSnapshot& snapshot) {
  auto report = validateSessionSnapshotState(snapshot.assets(), snapshot.collections());

  // Collection roles are typed by convention: a sequence slot must point at a
  // sequence asset, not merely any asset with that ID.
  for (const auto& collection : snapshot.collections()) {
    if (collection.sequence) {
      validateCollectionAssetRef(report, snapshot, *collection.sequence, CollectionAssetRole::Sequence);
    }
    for (const auto id : collection.instrumentSets) {
      validateCollectionAssetRef(report, snapshot, id, CollectionAssetRole::InstrumentSet);
    }
    for (const auto id : collection.sampleCollections) {
      validateCollectionAssetRef(report, snapshot, id, CollectionAssetRole::SampleCollection);
    }
    for (const auto id : collection.miscAssets) {
      validateCollectionAssetRef(report, snapshot, id, CollectionAssetRole::Misc);
    }
  }

  return report;
}

}  // namespace vgmtrans::core
