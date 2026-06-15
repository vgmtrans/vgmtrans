/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/CollectionStore.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string collectionKeyString(const CollectionKey& key) {
  return key.resolver + '\x1f' + key.value;
}

[[nodiscard]] bool referencesAnyAsset(const Collection& collection, const std::unordered_set<u32>& assetIds) {
  const auto referencesOne = [&](std::optional<AssetId> id) { return id && assetIds.contains(id->value); };
  const auto referencesMany = [&](const std::vector<AssetId>& ids) {
    return std::ranges::any_of(ids, [&](AssetId id) { return assetIds.contains(id.value); });
  };
  return referencesOne(collection.sequence) || referencesMany(collection.instrumentSets) ||
         referencesMany(collection.sampleCollections) || referencesMany(collection.miscAssets);
}

[[nodiscard]] bool hasIssueCode(const Collection& collection, std::string_view code) {
  return std::ranges::any_of(collection.issues, [code](const CollectionIssue& issue) { return issue.code == code; });
}

[[nodiscard]] CollectionStatus statusWithIssues(const DesiredCollection& collection) {
  if (collection.status != CollectionStatus::Complete) {
    return collection.status;
  }
  const bool hasError = std::ranges::any_of(
      collection.issues, [](const CollectionIssue& issue) { return issue.severity == Severity::Error; });
  return hasError ? CollectionStatus::Incomplete : CollectionStatus::Complete;
}

}  // namespace

void CollectionStore::reconcile(std::string_view resolverId, std::vector<DesiredCollection> desiredCollections,
                                const AssetStore& assets, DiagnosticStore& diagnostics, ScanIdAllocator& ids) {
  std::set<std::string> seenKeys;
  for (auto& desired : desiredCollections) {
    if (desired.key.resolver.empty()) {
      desired.key.resolver = std::string(resolverId);
    }
    if (desired.key.resolver != resolverId) {
      diagnostics.addError("Collection resolver '" + std::string(resolverId) +
                           "' returned a collection for resolver '" + desired.key.resolver + "'");
      continue;
    }
    if (desired.key.value.empty()) {
      diagnostics.addError("Collection resolver '" + std::string(resolverId) +
                           "' returned a collection with an empty key");
      continue;
    }

    const auto key = collectionKeyString(desired.key);
    if (!seenKeys.insert(key).second) {
      diagnostics.addError("Collection resolver '" + std::string(resolverId) + "' returned duplicate collection key '" +
                           desired.key.value + "'");
      continue;
    }

    validateAssetReferences(resolverId, desired, assets, diagnostics);

    const auto sameKey = [&](const Collection& collection) { return collection.key == desired.key; };
    if (auto found = std::ranges::find_if(collections_, sameKey); found != collections_.end()) {
      if (found->origin == CollectionOrigin::UserCreated) {
        found->status = CollectionStatus::Stale;
        continue;
      }
      found->name = desired.name;
      found->status = statusWithIssues(desired);
      found->origin = desired.origin;
      found->sequence = desired.sequence;
      found->instrumentSets = desired.instrumentSets;
      found->sampleCollections = desired.sampleCollections;
      found->miscAssets = desired.miscAssets;
      found->issues = std::move(desired.issues);
      continue;
    }

    collections_.push_back(Collection{
        .id = nextCollectionId(ids),
        .name = desired.name,
        .status = statusWithIssues(desired),
        .origin = desired.origin,
        .key = desired.key,
        .sequence = desired.sequence,
        .instrumentSets = desired.instrumentSets,
        .sampleCollections = desired.sampleCollections,
        .miscAssets = desired.miscAssets,
        .issues = std::move(desired.issues),
    });
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.origin == CollectionOrigin::Discovered && collection.key.resolver == resolverId &&
           !seenKeys.contains(collectionKeyString(collection.key));
  });
}

void CollectionStore::markStaleForAssets(const std::unordered_set<u32>& assetIds) {
  if (assetIds.empty()) {
    return;
  }

  for (auto& collection : collections_) {
    if (!referencesAnyAsset(collection, assetIds)) {
      continue;
    }

    collection.status = CollectionStatus::Stale;
    if (!hasIssueCode(collection, "removed-asset")) {
      collection.issues.push_back(CollectionIssue{
          .severity = Severity::Error,
          .code = "removed-asset",
          .message = "Collection references an asset from a removed source",
      });
    }
  }
}

CollectionId CollectionStore::nextCollectionId(ScanIdAllocator& ids) {
  CollectionId id;
  do {
    id = ids.nextCollectionId();
  } while (std::ranges::any_of(collections_, [id](const Collection& collection) { return collection.id == id; }));
  return id;
}

void CollectionStore::validateAssetReferences(std::string_view resolverId, DesiredCollection& desired,
                                              const AssetStore& assets, DiagnosticStore& diagnostics) {
  const auto addMissingAssetDiagnostic = [&](AssetId id, std::string_view role) {
    diagnostics.addError("Collection resolver '" + std::string(resolverId) + "' returned " + std::string(role) +
                         " asset id " + std::to_string(id.value) + " that does not exist");
    desired.issues.push_back(CollectionIssue{
        .severity = Severity::Error,
        .code = "missing-" + std::string(role),
        .message = "Collection references missing " + std::string(role) + " asset " + std::to_string(id.value),
        .asset = id,
    });
  };

  if (desired.sequence && !assets.contains(*desired.sequence)) {
    addMissingAssetDiagnostic(*desired.sequence, "sequence");
    desired.sequence = std::nullopt;
  }

  const auto filterExistingAssets = [&](std::vector<AssetId>& ids, std::string_view role) {
    std::erase_if(ids, [&](AssetId id) {
      if (assets.contains(id)) {
        return false;
      }
      addMissingAssetDiagnostic(id, role);
      return true;
    });
  };

  filterExistingAssets(desired.instrumentSets, "instrument-set");
  filterExistingAssets(desired.sampleCollections, "sample-collection");
  filterExistingAssets(desired.miscAssets, "misc");
}

}  // namespace vgmtrans::core
