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

template <typename T>
[[nodiscard]] std::shared_ptr<const std::vector<T>> sharedVector(std::vector<T> values) {
  return std::make_shared<const std::vector<T>>(std::move(values));
}

template <typename T, typename Predicate>
[[nodiscard]] std::shared_ptr<const std::vector<T>> without(const std::shared_ptr<const std::vector<T>>& values,
                                                            Predicate remove) {
  if (!std::ranges::any_of(*values, remove)) {
    return values;
  }
  std::vector<T> filtered;
  filtered.reserve(values->size());
  for (const auto& value : *values) {
    if (!remove(value)) {
      filtered.push_back(value);
    }
  }
  return sharedVector(std::move(filtered));
}

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
  return found != assetsById_.end() ? found->second : nullptr;
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

  for (const auto& annotation : result.sourceMap.annotations()) {
    if (annotation.id.valid() && annotationsById_.contains(annotation.id.value)) {
      throw std::invalid_argument("Source map reused existing annotation id " + std::to_string(annotation.id.value));
    }
  }

  assetsById_.reserve(assetsById_.size() + result.assets.size());
  annotationsById_.reserve(annotationsById_.size() + result.sourceMap.annotations().size());

  auto assets = sharedVector(std::move(result.assets));
  auto matchFacts = sharedVector(std::move(result.matchFacts));
  auto explicitCollections = sharedVector(std::move(result.explicitCollections));
  SourceMap sourceMap = std::move(result.sourceMap);
  auto diagnostics = std::move(result.diagnostics);

  if (!assets->empty() || !matchFacts->empty() || !explicitCollections->empty() || !sourceMap.empty()) {
    auto chunk = std::make_shared<const ScanChunk>(ScanChunk{
        .origin = origin,
        .assets = std::move(assets),
        .matchFacts = std::move(matchFacts),
        .explicitCollections = std::move(explicitCollections),
        .sourceMap = std::move(sourceMap),
    });
    scanChunks_.push_back(chunk);

    for (const auto& value : *chunk->assets) {
      assetsById_.emplace(metadata(value).id.value, &value);
    }
    for (const auto& annotation : chunk->sourceMap.annotations()) {
      if (annotation.id.valid()) {
        annotationsById_.emplace(annotation.id.value, &annotation);
      }
    }
  }
  diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(diagnostics.begin()),
                      std::make_move_iterator(diagnostics.end()));
  rebuildViews();
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
  return sourceMap_;
}

SourceMap SessionState::sourceMapForAsset(AssetId asset) const {
  std::vector<SourceAnnotation> selected;
  for (const auto& annotation : sourceMap_.annotations()) {
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
  for (const auto& chunk : scanChunks_) {
    for (const auto& collection : *chunk->explicitCollections) {
      if (!collection.key.resolver.empty()) {
        grouped[collection.key.resolver].push_back(desiredCollection(collection));
      }
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
  for (const auto& annotation : sourceMap_.annotations()) {
    const bool removedSource = annotation.range.valid() && sourceIds.contains(annotation.range.source.value);
    const auto owner = annotationAssetOwner(annotation.id);
    if (removedSource || (owner && assetIds.contains(owner->value))) {
      removedAnnotations.insert(annotation.id.value);
    }
  }

  const auto removesAsset = [&](const Asset& value) {
    const AssetId id = metadata(value).id;
    return id.valid() && assetIds.contains(id.value);
  };
  const auto removesFact = [&](const MatchFact& fact) {
    const bool removedSource = fact.scope.source && sourceIds.contains(fact.scope.source->value);
    return removedSource || (fact.asset.valid() && assetIds.contains(fact.asset.value));
  };
  const auto removesLink = [&](const SourceLink& link) {
    if (const auto* target = std::get_if<SourceRange>(&link.target)) {
      return target->valid() && sourceIds.contains(target->source.value);
    }
    if (const auto* target = std::get_if<SourceAnnotationId>(&link.target)) {
      return removedAnnotations.contains(target->value);
    }
    const auto* target = std::get_if<ObjectRef>(&link.target);
    return target != nullptr && target->asset.valid() && assetIds.contains(target->asset.value);
  };
  const auto removesDiagnostic = [&](const Diagnostic& diagnostic) {
    const bool removedSource = diagnostic.range && sourceIds.contains(diagnostic.range->source.value);
    const bool removedObject =
        diagnostic.object && diagnostic.object->asset.valid() && assetIds.contains(diagnostic.object->asset.value);
    const bool removedAnnotation = diagnostic.annotation && removedAnnotations.contains(diagnostic.annotation->value);
    return removedSource || removedObject || removedAnnotation;
  };

  std::vector<std::shared_ptr<const ScanChunk>> remainingChunks;
  remainingChunks.reserve(scanChunks_.size());
  for (const auto& chunk : scanChunks_) {
    auto assets = without(chunk->assets, removesAsset);
    auto matchFacts = without(chunk->matchFacts, removesFact);
    auto explicitCollections = without(chunk->explicitCollections, [&](const ExplicitCollection& collection) {
      return sourceIds.contains(chunk->origin.value) || referencesAnyAsset(collection, assetIds);
    });

    SourceMap sourceMap = chunk->sourceMap;
    const bool sourceMapChanged = std::ranges::any_of(sourceMap.annotations(), [&](const SourceAnnotation& annotation) {
      return removedAnnotations.contains(annotation.id.value) ||
             (annotation.parent && removedAnnotations.contains(annotation.parent->value)) ||
             std::ranges::any_of(annotation.links, removesLink);
    });
    if (sourceMapChanged) {
      std::vector<SourceAnnotation> annotations;
      annotations.reserve(sourceMap.annotations().size());
      for (const auto& value : sourceMap.annotations()) {
        if (removedAnnotations.contains(value.id.value)) {
          continue;
        }
        auto annotation = value;
        if (annotation.parent && removedAnnotations.contains(annotation.parent->value)) {
          annotation.parent.reset();
        }
        std::erase_if(annotation.links, removesLink);
        annotations.push_back(std::move(annotation));
      }
      sourceMap = SourceMap{std::move(annotations)};
    }

    const bool changed = assets != chunk->assets || matchFacts != chunk->matchFacts ||
                         explicitCollections != chunk->explicitCollections || sourceMapChanged;
    if (!changed) {
      remainingChunks.push_back(chunk);
      continue;
    }
    if (assets->empty() && matchFacts->empty() && explicitCollections->empty() && sourceMap.empty()) {
      continue;
    }
    remainingChunks.push_back(std::make_shared<const ScanChunk>(ScanChunk{
        .origin = chunk->origin,
        .assets = std::move(assets),
        .matchFacts = std::move(matchFacts),
        .explicitCollections = std::move(explicitCollections),
        .sourceMap = std::move(sourceMap),
    }));
  }
  scanChunks_ = std::move(remainingChunks);

  std::erase_if(diagnostics_, removesDiagnostic);

  markCollectionsStaleForAssets(assetIds);
  rebuildViews();
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
  for (size_t depth = 0; id.valid() && depth < sourceMap_.annotations().size(); ++depth) {
    const auto found = annotationsById_.find(id.value);
    if (found == annotationsById_.end()) {
      break;
    }
    const auto& annotation = *found->second;
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

void SessionState::rebuildViews() {
  std::vector<std::shared_ptr<const std::vector<Asset>>> assetChunks;
  std::vector<std::shared_ptr<const std::vector<MatchFact>>> factChunks;
  std::vector<SourceMap> sourceMaps;
  assetChunks.reserve(scanChunks_.size());
  factChunks.reserve(scanChunks_.size());
  sourceMaps.reserve(scanChunks_.size());
  for (const auto& chunk : scanChunks_) {
    assetChunks.push_back(chunk->assets);
    factChunks.push_back(chunk->matchFacts);
    sourceMaps.push_back(chunk->sourceMap);
  }

  assets_ = detail::SharedSequenceAccess::fromChunks(std::move(assetChunks));
  matchFacts_ = detail::SharedSequenceAccess::fromChunks(std::move(factChunks));
  sourceMap_ = SourceMap::join(sourceMaps);
}

void SessionState::rebuildIndexes() {
  assetsById_.clear();
  assetsById_.reserve(assets_.size());
  for (const auto& asset : assets_) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, &asset);
    }
  }

  annotationsById_.clear();
  annotationsById_.reserve(sourceMap_.annotations().size());
  for (const auto& annotation : sourceMap_.annotations()) {
    if (annotation.id.valid()) {
      annotationsById_.emplace(annotation.id.value, &annotation);
    }
  }
}

}  // namespace vgmtrans::core
