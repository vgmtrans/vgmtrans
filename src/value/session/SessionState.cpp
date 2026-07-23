/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SessionState.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

template <typename CollectionT>
[[nodiscard]] bool referencesAnyAsset(const CollectionT& collection, const std::unordered_set<u32>& assetIds) {
  const auto referencesOne = [&](std::optional<AssetId> id) { return id && assetIds.contains(id->value); };
  const auto referencesMany = [&](const std::vector<AssetId>& ids) {
    return std::ranges::any_of(ids, [&](AssetId id) { return assetIds.contains(id.value); });
  };
  return referencesOne(collection.sequence) || referencesMany(collection.instrumentSets) ||
         referencesMany(collection.sampleCollections) || referencesMany(collection.miscAssets);
}

[[nodiscard]] DesiredCollection desiredCollection(const ExplicitCollection& collection) {
  return DesiredCollection{
      .key = collection.key,
      .name = collection.name,
      .sequence = collection.sequence,
      .instrumentSets = collection.instrumentSets,
      .sampleCollections = collection.sampleCollections,
      .miscAssets = collection.miscAssets,
  };
}

[[nodiscard]] std::string collectionKeyString(const CollectionKey& key) {
  return key.resolver + '\x1f' + key.value;
}

[[nodiscard]] bool hasIssueCode(const Collection& collection, std::string_view code) {
  return std::ranges::any_of(collection.issues, [code](const CollectionIssue& issue) { return issue.code == code; });
}

}  // namespace

bool SessionState::containsAsset(AssetId id) const noexcept {
  return id.valid() && assetsById_.contains(id.value);
}

const Asset* SessionState::asset(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }
  const auto found = assetsById_.find(id.value);
  return found != assetsById_.end() && found->second < assets_.size() ? &assets_[found->second] : nullptr;
}

void SessionState::appendScan(SourceId origin, ScanResult result) {
  std::unordered_set<u32> batchAssetIds;
  batchAssetIds.reserve(result.assets.size());
  for (const auto& value : result.assets) {
    const AssetId id = metadata(value).id;
    if (!id.valid()) {
      throw std::invalid_argument("Scan result contained an asset without an id");
    }
    if (!batchAssetIds.insert(id.value).second) {
      throw std::invalid_argument("Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (assetsById_.contains(id.value)) {
      throw std::invalid_argument("Scan result reused existing asset id " + std::to_string(id.value));
    }
  }

  for (const auto& annotation : result.sourceMap.annotations_) {
    if (annotation.id.valid() && annotationsById_.contains(annotation.id.value)) {
      throw std::invalid_argument("Source map reused existing annotation id " + std::to_string(annotation.id.value));
    }
  }

  assets_.reserve(assets_.size() + result.assets.size());
  matchFacts_.reserve(matchFacts_.size() + result.matchFacts.size());
  explicitCollections_.reserve(explicitCollections_.size() + result.explicitCollections.size());
  annotations_.reserve(annotations_.size() + result.sourceMap.annotations_.size());
  diagnostics_.reserve(diagnostics_.size() + result.diagnostics.size());
  assetsById_.reserve(assetsById_.size() + result.assets.size());
  annotationsById_.reserve(annotationsById_.size() + result.sourceMap.annotations_.size());

  for (auto& value : result.assets) {
    const AssetId id = metadata(value).id;
    assetsById_.emplace(id.value, assets_.size());
    assets_.push_back(std::move(value));
  }
  matchFacts_.insert(matchFacts_.end(), std::make_move_iterator(result.matchFacts.begin()),
                     std::make_move_iterator(result.matchFacts.end()));
  for (auto& collection : result.explicitCollections) {
    explicitCollections_.push_back(ExplicitCollectionEntry{
        .origin = origin,
        .collection = std::move(collection),
    });
  }
  for (auto& annotation : result.sourceMap.annotations_) {
    if (annotation.id.valid()) {
      annotationsById_.emplace(annotation.id.value, annotations_.size());
    }
    annotations_.push_back(std::move(annotation));
  }
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(result.diagnostics.begin()),
                      std::make_move_iterator(result.diagnostics.end()));
}

bool SessionState::removeAssets(std::span<const AssetId> assets) {
  std::unordered_set<u32> removed;
  removed.reserve(assets.size());
  for (const AssetId id : assets) {
    if (containsAsset(id)) {
      removed.insert(id.value);
    }
  }
  if (removed.empty()) {
    return false;
  }
  removeDiscoveredData({}, removed);
  return true;
}

void SessionState::removeSources(std::span<const SourceId> sources) {
  std::unordered_set<u32> sourceIds;
  sourceIds.reserve(sources.size());
  for (const SourceId source : sources) {
    if (source.valid()) {
      sourceIds.insert(source.value);
    }
  }

  std::unordered_set<u32> removedAssets;
  for (const auto& value : assets_) {
    const auto& meta = metadata(value);
    if (meta.id.valid() && meta.range.valid() && sourceIds.contains(meta.range.source.value)) {
      removedAssets.insert(meta.id.value);
    }
  }
  removeDiscoveredData(sourceIds, removedAssets);
}

void SessionState::addError(std::string message, std::optional<SourceRange> range) {
  diagnostics_.push_back(Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
  });
}

void SessionState::addDiagnostics(std::vector<Diagnostic> diagnostics) {
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(diagnostics.begin()),
                      std::make_move_iterator(diagnostics.end()));
}

SourceMap SessionState::sourceMap() const {
  return SourceMap{annotations_};
}

SourceMap SessionState::sourceMapForAsset(AssetId asset) const {
  std::vector<SourceAnnotation> selected;
  for (const auto& annotation : annotations_) {
    if (annotationAssetOwner(annotation.id) == asset) {
      selected.push_back(annotation);
    }
  }
  return SourceMap{std::move(selected)};
}

std::map<std::string, std::vector<DesiredCollection>> SessionState::desiredCollectionsByResolver() const {
  std::map<std::string, std::vector<DesiredCollection>> grouped;
  for (const auto& collection : collections_) {
    if (collection.origin == CollectionOrigin::Discovered && !collection.key.resolver.empty()) {
      grouped.try_emplace(collection.key.resolver);
    }
  }
  for (const auto& entry : explicitCollections_) {
    if (!entry.collection.key.resolver.empty()) {
      grouped[entry.collection.key.resolver].push_back(desiredCollection(entry.collection));
    }
  }
  return grouped;
}

void SessionState::reconcileCollections(std::string_view resolver, std::vector<DesiredCollection> desired,
                                        ScanIdAllocator& ids) {
  std::set<std::string> seenKeys;
  for (auto& candidate : desired) {
    if (candidate.key.resolver.empty()) {
      candidate.key.resolver = std::string(resolver);
    }
    if (candidate.key.resolver != resolver) {
      addError("Collection resolver '" + std::string(resolver) + "' returned a collection for resolver '" +
               candidate.key.resolver + "'");
      continue;
    }
    if (candidate.key.value.empty()) {
      addError("Collection resolver '" + std::string(resolver) + "' returned a collection with an empty key");
      continue;
    }

    const auto key = collectionKeyString(candidate.key);
    if (!seenKeys.insert(key).second) {
      addError("Collection resolver '" + std::string(resolver) + "' returned duplicate collection key '" +
               candidate.key.value + "'");
      continue;
    }

    validateCollectionAssetReferences(resolver, candidate);
    const auto sameKey = [&](const Collection& collection) { return collection.key == candidate.key; };
    if (auto found = std::ranges::find_if(collections_, sameKey); found != collections_.end()) {
      if (found->origin == CollectionOrigin::UserCreated) {
        found->status = CollectionStatus::Stale;
        continue;
      }
      found->name = candidate.name;
      found->status = validatedCollectionStatus(candidate);
      found->sequence = candidate.sequence;
      found->instrumentSets = candidate.instrumentSets;
      found->sampleCollections = candidate.sampleCollections;
      found->miscAssets = candidate.miscAssets;
      found->issues = std::move(candidate.issues);
      continue;
    }

    collections_.push_back(Collection{
        .id = nextCollectionId(ids),
        .name = candidate.name,
        .status = validatedCollectionStatus(candidate),
        .origin = CollectionOrigin::Discovered,
        .key = candidate.key,
        .sequence = candidate.sequence,
        .instrumentSets = candidate.instrumentSets,
        .sampleCollections = candidate.sampleCollections,
        .miscAssets = candidate.miscAssets,
        .issues = std::move(candidate.issues),
    });
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.origin == CollectionOrigin::Discovered && collection.key.resolver == resolver &&
           !seenKeys.contains(collectionKeyString(collection.key));
  });
}

void SessionState::removeDiscoveredData(const std::unordered_set<u32>& sourceIds,
                                        const std::unordered_set<u32>& assetIds) {
  if (sourceIds.empty() && assetIds.empty()) {
    return;
  }

  std::unordered_set<u32> removedAnnotations;
  for (const auto& annotation : annotations_) {
    const bool removedSource = annotation.range.valid() && sourceIds.contains(annotation.range.source.value);
    const auto owner = annotationAssetOwner(annotation.id);
    if (removedSource || (owner && assetIds.contains(owner->value))) {
      removedAnnotations.insert(annotation.id.value);
    }
  }

  std::erase_if(assets_, [&](const Asset& value) {
    const AssetId id = metadata(value).id;
    return id.valid() && assetIds.contains(id.value);
  });
  std::erase_if(matchFacts_, [&](const MatchFact& fact) {
    const bool removedSource = fact.scope.source && sourceIds.contains(fact.scope.source->value);
    return removedSource || (fact.asset.valid() && assetIds.contains(fact.asset.value));
  });
  std::erase_if(explicitCollections_, [&](const ExplicitCollectionEntry& entry) {
    return sourceIds.contains(entry.origin.value) || referencesAnyAsset(entry.collection, assetIds);
  });

  for (auto& annotation : annotations_) {
    if (annotation.parent && removedAnnotations.contains(annotation.parent->value)) {
      annotation.parent.reset();
    }
    std::erase_if(annotation.links, [&](const SourceLink& link) {
      if (const auto* target = std::get_if<SourceRange>(&link.target)) {
        return target->valid() && sourceIds.contains(target->source.value);
      }
      if (const auto* target = std::get_if<SourceAnnotationId>(&link.target)) {
        return removedAnnotations.contains(target->value);
      }
      const auto* target = std::get_if<ObjectRef>(&link.target);
      return target != nullptr && target->asset.valid() && assetIds.contains(target->asset.value);
    });
  }
  std::erase_if(annotations_,
                [&](const SourceAnnotation& annotation) { return removedAnnotations.contains(annotation.id.value); });
  std::erase_if(diagnostics_, [&](const Diagnostic& diagnostic) {
    const bool removedSource = diagnostic.range && sourceIds.contains(diagnostic.range->source.value);
    const bool removedObject =
        diagnostic.object && diagnostic.object->asset.valid() && assetIds.contains(diagnostic.object->asset.value);
    const bool removedAnnotation = diagnostic.annotation && removedAnnotations.contains(diagnostic.annotation->value);
    return removedSource || removedObject || removedAnnotation;
  });

  markCollectionsStaleForAssets(assetIds);
  rebuildIndexes();
}

void SessionState::markCollectionsStaleForAssets(const std::unordered_set<u32>& assetIds) {
  if (assetIds.empty()) {
    return;
  }
  for (auto& collection : collections_) {
    if (!referencesAnyAsset(collection, assetIds)) {
      continue;
    }
    collection.status = CollectionStatus::Stale;
    if (!hasIssueCode(collection, "removed-asset")) {
      collection.issues.push_back(removedStaleAssetIssue());
    }
  }
}

void SessionState::validateCollectionAssetReferences(std::string_view resolver, DesiredCollection& desired) {
  const auto addMissing = [&](AssetId id, std::string_view role) {
    addError("Collection resolver '" + std::string(resolver) + "' returned " + std::string(role) + " asset id " +
             std::to_string(id.value) + " that does not exist");
    if (role == "sequence") {
      desired.issues.push_back(missingSequenceIssue(id));
    } else if (role == "instrument-set") {
      desired.issues.push_back(missingInstrumentSetIssue(id));
    } else if (role == "sample-collection") {
      desired.issues.push_back(missingSampleCollectionIssue(id));
    } else {
      desired.issues.push_back(CollectionIssue{
          .severity = Severity::Error,
          .code = "missing-" + std::string(role),
          .message = "Collection references missing " + std::string(role) + " asset " + std::to_string(id.value),
          .asset = id,
      });
    }
  };
  const auto addWrongType = [&](AssetId id, std::string_view role, std::string_view article) {
    addError("Collection resolver '" + std::string(resolver) + "' returned " + std::string(role) + " asset id " +
             std::to_string(id.value) + " that is not " + std::string(article) + " " + std::string(role) + " asset");
    desired.issues.push_back(CollectionIssue{
        .severity = Severity::Error,
        .code = "wrong-type-" + std::string(role),
        .message = "Collection references wrong-type " + std::string(role) + " asset " + std::to_string(id.value),
        .asset = id,
    });
  };

  if (desired.sequence) {
    if (!containsAsset(*desired.sequence)) {
      addMissing(*desired.sequence, "sequence");
      desired.sequence.reset();
    } else if (asset<SequenceProgramAsset>(*desired.sequence) == nullptr) {
      addWrongType(*desired.sequence, "sequence", "a");
      desired.sequence.reset();
    }
  }

  const auto filter = [&](std::vector<AssetId>& ids, std::string_view role, std::string_view article,
                          auto hasExpectedType) {
    std::erase_if(ids, [&](AssetId id) {
      if (!containsAsset(id)) {
        addMissing(id, role);
        return true;
      }
      if (hasExpectedType(id)) {
        return false;
      }
      addWrongType(id, role, article);
      return true;
    });
  };
  filter(desired.instrumentSets, "instrument-set", "an",
         [&](AssetId id) { return asset<InstrumentSetAsset>(id) != nullptr; });
  filter(desired.sampleCollections, "sample-collection", "a",
         [&](AssetId id) { return asset<SampleCollectionAsset>(id) != nullptr; });
  filter(desired.miscAssets, "misc", "a", [&](AssetId id) { return asset<MiscAsset>(id) != nullptr; });
}

CollectionId SessionState::nextCollectionId(ScanIdAllocator& ids) const {
  CollectionId id;
  do {
    id = ids.nextCollectionId();
  } while (std::ranges::any_of(collections_, [id](const Collection& collection) { return collection.id == id; }));
  return id;
}

std::optional<AssetId> SessionState::annotationAssetOwner(SourceAnnotationId id) const {
  for (size_t depth = 0; id.valid() && depth < annotations_.size(); ++depth) {
    const auto found = annotationsById_.find(id.value);
    if (found == annotationsById_.end() || found->second >= annotations_.size()) {
      break;
    }
    const auto& annotation = annotations_[found->second];
    if (annotation.owner && annotation.owner->asset.valid()) {
      return annotation.owner->asset;
    }
    if (!annotation.parent) {
      break;
    }
    id = *annotation.parent;
  }
  return std::nullopt;
}

void SessionState::rebuildIndexes() {
  assetsById_.clear();
  assetsById_.reserve(assets_.size());
  for (size_t index = 0; index < assets_.size(); ++index) {
    const AssetId id = metadata(assets_[index]).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, index);
    }
  }

  annotationsById_.clear();
  annotationsById_.reserve(annotations_.size());
  for (size_t index = 0; index < annotations_.size(); ++index) {
    const auto& annotation = annotations_[index];
    if (annotation.id.valid()) {
      annotationsById_.emplace(annotation.id.value, index);
    }
  }
}

}  // namespace vgmtrans::core
