/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SnapshotValidation.h"

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
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

void collectionFinding(ValidationReport& report, const Collection& collection, Severity severity, std::string code,
                       std::string message, std::optional<SourceRange> range = std::nullopt,
                       std::optional<AssetId> asset = std::nullopt) {
  report.add(ValidationFinding{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
      .asset = asset,
      .collection = collection.id,
  });
}

void validateCollectionSynth(ValidationReport& report, const SessionSnapshot& snapshot, const Collection& collection) {
  std::unordered_map<u32, const SampleCollectionAsset*> sampleCollections;
  for (const AssetId id : collection.sampleCollections) {
    if (const auto* samples = snapshot.asset<SampleCollectionAsset>(id)) {
      sampleCollections.emplace(id.value, samples);
    }
  }

  std::map<std::pair<std::string, u32>, AssetId> identities;
  std::map<std::pair<u32, u32>, AssetId> explicitAddresses;
  for (const AssetId instrumentSetId : collection.instrumentSets) {
    const auto* instrumentSet = snapshot.asset<InstrumentSetAsset>(instrumentSetId);
    if (instrumentSet == nullptr) {
      continue;
    }
    for (const auto& instrument : instrumentSet->instruments) {
      if (instrument.identity && instrument.identity->valid()) {
        const auto key = std::pair{instrument.identity->domain, instrument.identity->key};
        const auto [found, inserted] = identities.try_emplace(key, instrumentSetId);
        if (!inserted && found->second != instrumentSetId) {
          collectionFinding(report, collection, Severity::Error, "snapshot.collection.duplicate-instrument-identity",
                            "Collection contains the same instrument identity in more than one instrument set",
                            instrument.range.valid() ? std::optional<SourceRange>{instrument.range} : std::nullopt,
                            instrumentSetId);
        }
      }
      if (instrument.explicitAddress) {
        const auto key = std::pair{instrument.explicitAddress->bank, instrument.explicitAddress->program};
        const auto [found, inserted] = explicitAddresses.try_emplace(key, instrumentSetId);
        if (!inserted && found->second != instrumentSetId) {
          collectionFinding(report, collection, Severity::Warning, "snapshot.collection.conflicting-instrument-address",
                            "Collection contains the same explicit bank and program in more than one instrument set",
                            instrument.range.valid() ? std::optional<SourceRange>{instrument.range} : std::nullopt,
                            instrumentSetId);
        }
      }

      for (const auto& region : instrument.regions) {
        const SampleCollectionAsset* samples = nullptr;
        // Older values omitted the sample collection because one collection
        // was the only possible target. Preserve that case, but never guess
        // when no target or several targets exist.
        if (region.sample.collection) {
          const auto found = sampleCollections.find(region.sample.collection->value);
          if (found == sampleCollections.end()) {
            collectionFinding(
                report, collection, Severity::Error, "snapshot.collection.region-sample-collection-missing",
                "Instrument region references a sample collection that is not attached to its collection",
                region.range.valid() ? std::optional<SourceRange>{region.range} : std::nullopt, instrumentSetId);
            continue;
          }
          samples = found->second;
        } else if (sampleCollections.empty()) {
          collectionFinding(report, collection, Severity::Error, "snapshot.collection.region-sample-collection-missing",
                            "Instrument region has no sample collection to use",
                            region.range.valid() ? std::optional<SourceRange>{region.range} : std::nullopt,
                            instrumentSetId);
          continue;
        } else if (sampleCollections.size() > 1) {
          collectionFinding(
              report, collection, Severity::Error, "snapshot.collection.region-sample-collection-ambiguous",
              "Instrument region omits its sample collection in a collection with several sample sets",
              region.range.valid() ? std::optional<SourceRange>{region.range} : std::nullopt, instrumentSetId);
          continue;
        } else {
          samples = sampleCollections.begin()->second;
        }

        if (region.sample.index >= samples->samples.samples.size()) {
          collectionFinding(report, collection, Severity::Error, "snapshot.collection.region-sample-index-invalid",
                            "Instrument region sample index is outside its referenced sample collection",
                            region.range.valid() ? std::optional<SourceRange>{region.range} : std::nullopt,
                            instrumentSetId);
        }
      }
    }
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
    validateCollectionSynth(report, snapshot, collection);
  }

  return report;
}

}  // namespace vgmtrans::core
