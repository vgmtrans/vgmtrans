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
  if (values.empty()) {
    static const auto empty = std::make_shared<const std::vector<T>>();
    return empty;
  }
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

[[nodiscard]] std::optional<AssetId> referencedAsset(const CollectionMembers& members,
                                                     const std::unordered_set<u32>& assetIds) {
  if (members.sequence && assetIds.contains(members.sequence->value)) {
    return members.sequence;
  }
  const auto find = [&](const std::vector<AssetId>& ids) -> std::optional<AssetId> {
    const auto found = std::ranges::find_if(ids, [&](AssetId id) { return assetIds.contains(id.value); });
    return found != ids.end() ? std::optional{*found} : std::nullopt;
  };
  if (auto found = find(members.soundBanks)) {
    return found;
  }
  if (auto found = find(members.samplePools)) {
    return found;
  }
  return find(members.miscAssets);
}

[[nodiscard]] DesiredCollection desiredCollection(const ExplicitCollection& collection) {
  return DesiredCollection{
      .localKey = collection.key.value,
      .name = collection.name,
      .members = collection.members,
  };
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
    if (annotation.id.valid() && annotationIds_.contains(annotation.id.value)) {
      throw std::invalid_argument("Source map reused existing annotation id " + std::to_string(annotation.id.value));
    }
  }

  if (!result.assets.empty() || !result.sourceMap.empty()) {
    scanChunks_.push_back(ScanChunk{
        .assets = sharedVector(std::move(result.assets)),
        .sourceMap = std::move(result.sourceMap),
    });
    const auto& chunk = scanChunks_.back();

    for (const auto& value : *chunk.assets) {
      assetsById_.emplace(metadata(value).id.value, &value);
    }
    for (const auto& annotation : chunk.sourceMap.annotations()) {
      if (annotation.id.valid()) {
        annotationIds_.insert(annotation.id.value);
      }
    }
    rebuildViews();
  }

  explicitCollections_.reserve(explicitCollections_.size() + result.explicitCollections.size());
  for (auto& collection : result.explicitCollections) {
    explicitCollections_.push_back(ExplicitCollectionEntry{
        .origin = origin,
        .collection = std::move(collection),
    });
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

CollectionId SessionState::createUserCollection(std::string name, CollectionMembers members, CollectionBinder binder,
                                                ScanIdAllocator& ids) {
  if (name.empty()) {
    throw std::invalid_argument("A user-created collection must have a name");
  }
  if (!members.sequence) {
    throw std::invalid_argument("A user-created collection must contain a sequence");
  }
  if (asset<SequenceProgramAsset>(*members.sequence) == nullptr) {
    throw std::invalid_argument("The selected sequence asset does not exist or has the wrong type");
  }
  if (members.soundBanks.empty()) {
    throw std::invalid_argument("A user-created collection must contain a sound bank");
  }

  const auto validate = [](const std::vector<AssetId>& values, auto expected, std::string_view role) {
    std::unordered_set<u32> seen;
    for (const AssetId id : values) {
      if (!seen.insert(id.value).second) {
        throw std::invalid_argument("The selected " + std::string(role) + " asset is duplicated");
      }
      if (expected(id) == nullptr) {
        throw std::invalid_argument("A selected " + std::string(role) + " asset does not exist or has the wrong type");
      }
    }
  };
  validate(members.soundBanks, [this](AssetId id) { return asset<SoundBankAsset>(id); }, "sound bank");
  validate(members.samplePools, [this](AssetId id) { return asset<SamplePoolAsset>(id); }, "sample pool");
  validate(members.miscAssets, [this](AssetId id) { return asset<MiscAsset>(id); }, "miscellaneous");

  const CollectionId id = nextCollectionId(ids);
  collections_.push_back(Collection{
      .id = id,
      .name = std::move(name),
      .origin = CollectionOrigin::UserCreated,
      .binder = std::move(binder),
      .members = std::move(members),
  });
  return id;
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

SourceMap SessionState::sourceMapForAsset(AssetId asset) const {
  std::vector<SourceAnnotation> selected;
  const auto annotations = sourceMap_.annotationsForAsset(asset);
  selected.reserve(annotations.size());
  for (const SourceAnnotationId annotation : annotations) {
    selected.push_back(sourceMap_.get(annotation));
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
                                        CollectionBinder binder, ScanIdAllocator& ids) {
  std::set<std::string> seenKeys;
  for (auto& candidate : desired) {
    if (candidate.localKey.empty()) {
      addError("Collection resolver '" + std::string(resolver) + "' returned a collection with an empty key");
      continue;
    }

    if (!seenKeys.insert(candidate.localKey).second) {
      addError("Collection resolver '" + std::string(resolver) + "' returned duplicate collection key '" +
               candidate.localKey + "'");
      continue;
    }

    validateCollectionAssetReferences(resolver, candidate);
    CollectionBinder candidateBinder = candidate.binder ? std::move(candidate.binder) : binder;
    CollectionKey key{
        .resolver = std::string(resolver),
        .value = std::move(candidate.localKey),
    };
    const auto sameKey = [&](const Collection& collection) { return collection.key == key; };
    if (auto found = std::ranges::find_if(collections_, sameKey); found != collections_.end()) {
      found->name = std::move(candidate.name);
      found->binder = std::move(candidateBinder);
      found->members = std::move(candidate.members);
      found->issues = std::move(candidate.issues);
      continue;
    }

    collections_.push_back(Collection{
        .id = nextCollectionId(ids),
        .name = std::move(candidate.name),
        .origin = CollectionOrigin::Discovered,
        .key = std::move(key),
        .binder = std::move(candidateBinder),
        .members = std::move(candidate.members),
        .issues = std::move(candidate.issues),
    });
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.origin == CollectionOrigin::Discovered && collection.key.resolver == resolver &&
           !seenKeys.contains(collection.key.value);
  });
}

void SessionState::removeDiscoveredData(const std::unordered_set<u32>& sourceIds,
                                        const std::unordered_set<u32>& assetIds) {
  if (sourceIds.empty() && assetIds.empty()) {
    return;
  }

  std::unordered_set<u32> removedAnnotations;
  for (const u32 source : sourceIds) {
    for (const SourceAnnotationId annotation : sourceMap_.annotationsForSource(SourceId{source})) {
      removedAnnotations.insert(annotation.value);
    }
  }
  for (const u32 asset : assetIds) {
    for (const SourceAnnotationId annotation : sourceMap_.annotationsForAsset(AssetId{asset})) {
      removedAnnotations.insert(annotation.value);
    }
  }

  const auto removesAsset = [&](const Asset& value) {
    const AssetId id = metadata(value).id;
    return id.valid() && assetIds.contains(id.value);
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

  std::erase_if(explicitCollections_, [&](const ExplicitCollectionEntry& entry) {
    return sourceIds.contains(entry.origin.value) || referencedAsset(entry.collection.members, assetIds).has_value();
  });

  std::vector<ScanChunk> remainingChunks;
  remainingChunks.reserve(scanChunks_.size());
  for (const auto& chunk : scanChunks_) {
    auto assets = without(chunk.assets, removesAsset);

    SourceMap sourceMap = chunk.sourceMap;
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

    const bool changed = assets != chunk.assets || sourceMapChanged;
    if (!changed) {
      remainingChunks.push_back(chunk);
      continue;
    }
    ScanChunk filtered{
        .assets = std::move(assets),
        .sourceMap = std::move(sourceMap),
    };
    if (!filtered.empty()) {
      remainingChunks.push_back(std::move(filtered));
    }
  }
  scanChunks_ = std::move(remainingChunks);

  std::erase_if(diagnostics_, removesDiagnostic);

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.origin == CollectionOrigin::UserCreated && referencedAsset(collection.members, assetIds);
  });
  rebuildViews();
  rebuildIndexes();
}

void SessionState::validateCollectionAssetReferences(std::string_view resolver, DesiredCollection& desired) {
  const auto addMissing = [&](AssetId id, std::string_view role) {
    addError("Collection resolver '" + std::string(resolver) + "' returned " + std::string(role) + " asset id " +
             std::to_string(id.value) + " that does not exist");
    if (role == "sequence") {
      desired.issues.push_back(missingSequenceIssue(id));
    } else if (role == "sound-bank") {
      desired.issues.push_back(missingSoundBankIssue(id));
    } else if (role == "sample-pool") {
      desired.issues.push_back(missingSamplePoolIssue(id));
    } else {
      desired.issues.push_back(CollectionIssue{
          .impact = CollectionIssueImpact::Incomplete,
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
        .impact = CollectionIssueImpact::Incomplete,
        .severity = Severity::Error,
        .code = "wrong-type-" + std::string(role),
        .message = "Collection references wrong-type " + std::string(role) + " asset " + std::to_string(id.value),
        .asset = id,
    });
  };

  if (desired.members.sequence) {
    if (!containsAsset(*desired.members.sequence)) {
      addMissing(*desired.members.sequence, "sequence");
      desired.members.sequence.reset();
    } else if (asset<SequenceProgramAsset>(*desired.members.sequence) == nullptr) {
      addWrongType(*desired.members.sequence, "sequence", "a");
      desired.members.sequence.reset();
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
  filter(desired.members.soundBanks, "sound-bank", "a",
         [&](AssetId id) { return asset<SoundBankAsset>(id) != nullptr; });
  filter(desired.members.samplePools, "sample-pool", "a",
         [&](AssetId id) { return asset<SamplePoolAsset>(id) != nullptr; });
  filter(desired.members.miscAssets, "misc", "a", [&](AssetId id) { return asset<MiscAsset>(id) != nullptr; });
}

CollectionId SessionState::nextCollectionId(ScanIdAllocator& ids) const {
  CollectionId id;
  do {
    id = ids.nextCollectionId();
  } while (std::ranges::any_of(collections_, [id](const Collection& collection) { return collection.id == id; }));
  return id;
}

void SessionState::rebuildViews() {
  std::vector<std::shared_ptr<const std::vector<Asset>>> assetChunks;
  std::vector<SourceMap> sourceMaps;
  assetChunks.reserve(scanChunks_.size());
  sourceMaps.reserve(scanChunks_.size());
  for (const auto& chunk : scanChunks_) {
    assetChunks.push_back(chunk.assets);
    sourceMaps.push_back(chunk.sourceMap);
  }

  auto assets = detail::SharedSequenceAccess::fromChunks(std::move(assetChunks));
  auto sourceMap = SourceMap::join(sourceMaps);
  assets_ = std::move(assets);
  sourceMap_ = std::move(sourceMap);
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

  annotationIds_.clear();
  annotationIds_.reserve(sourceMap_.annotations().size());
  for (const auto& annotation : sourceMap_.annotations()) {
    if (annotation.id.valid()) {
      annotationIds_.insert(annotation.id.value);
    }
  }
}

}  // namespace vgmtrans::core
