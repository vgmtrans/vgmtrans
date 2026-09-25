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

[[nodiscard]] bool referencesAnyAsset(const CollectionMembers& members, const std::unordered_set<u32>& assetIds) {
  const auto matches = [&](AssetId id) { return assetIds.contains(id.value); };
  return (members.sequence && matches(*members.sequence)) || std::ranges::any_of(members.soundBanks, matches) ||
         std::ranges::any_of(members.samplePools, matches) || std::ranges::any_of(members.miscAssets, matches);
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

void SessionState::appendScan(ScanResult result) {
  // validateScanResult() has already admitted asset IDs. Annotation IDs need a
  // separate cross-scan check because validation does not see session annotations.
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

CollectionId SessionState::createUserCollection(std::string name, CollectionMembers members,
                                                std::vector<ResolvedDependency> dependencies,
                                                std::vector<CollectionIssue> issues) {
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

  const CollectionId id{nextCollectionId_++};
  collections_.push_back(Collection{
      .id = id,
      .name = std::move(name),
      .members = std::move(members),
      .issues = std::move(issues),
      .dependencies = std::move(dependencies),
  });
  return id;
}

void SessionState::addError(std::string message, SourceRange range) {
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

void SessionState::reconcileCollections(std::vector<DesiredCollection> desired) {
  std::ranges::stable_sort(desired, {}, [](const auto& candidate) { return candidate.key.resolver; });
  std::set<std::pair<std::string, std::string>> seenKeys;
  for (auto& candidate : desired) {
    const auto& resolver = candidate.key.resolver;
    if (candidate.key.value.empty()) {
      addError("Collection resolver '" + std::string(resolver) + "' returned a collection with an empty key");
      continue;
    }

    if (!seenKeys.emplace(resolver, candidate.key.value).second) {
      addError("Collection resolver '" + std::string(resolver) + "' returned duplicate collection key '" +
               candidate.key.value + "'");
      continue;
    }

    validateMiscAssets(candidate);
    Collection collection{
        .name = std::move(candidate.name),
        .key = std::move(candidate.key),
        .members = std::move(candidate.members),
        .issues = std::move(candidate.issues),
        .dependencies = std::move(candidate.dependencies),
    };
    if (auto found = std::ranges::find(collections_, collection.key, &Collection::key); found != collections_.end()) {
      collection.id = found->id;
      *found = std::move(collection);
    } else {
      collection.id = CollectionId{nextCollectionId_++};
      collections_.push_back(std::move(collection));
    }
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.key && !seenKeys.contains({collection.key->resolver, collection.key->value});
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
    const bool removedSource = diagnostic.range.valid() && sourceIds.contains(diagnostic.range.source.value);
    const bool removedObject =
        diagnostic.object && diagnostic.object->asset.valid() && assetIds.contains(diagnostic.object->asset.value);
    const bool removedAnnotation = diagnostic.annotation && removedAnnotations.contains(diagnostic.annotation->value);
    return removedSource || removedObject || removedAnnotation;
  };

  for (auto& chunk : scanChunks_) {
    chunk.assets = without(chunk.assets, removesAsset);

    if (std::ranges::any_of(chunk.sourceMap.annotations(), [&](const SourceAnnotation& annotation) {
          return removedAnnotations.contains(annotation.id.value) ||
                 (annotation.parent && removedAnnotations.contains(annotation.parent->value)) ||
                 std::ranges::any_of(annotation.links, removesLink);
        })) {
      std::vector<SourceAnnotation> annotations;
      annotations.reserve(chunk.sourceMap.annotations().size());
      for (const auto& value : chunk.sourceMap.annotations()) {
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
      chunk.sourceMap = SourceMap{std::move(annotations)};
    }
  }
  std::erase_if(scanChunks_, [](const ScanChunk& chunk) { return chunk.empty(); });

  std::erase_if(diagnostics_, removesDiagnostic);

  std::erase_if(collections_, [&](const Collection& collection) {
    return !collection.isDiscovered() && referencesAnyAsset(collection.members, assetIds);
  });
  rebuildViews();
  rebuildIndexes();
}

void SessionState::validateMiscAssets(DesiredCollection& desired) {
  // Resolution already validates the sequence and audio providers. Supplemental
  // inspection assets are direct scanner references and can disappear separately.
  std::erase_if(desired.members.miscAssets, [&](AssetId id) {
    if (asset<MiscAsset>(id) != nullptr) {
      return false;
    }
    const bool missing = !containsAsset(id);
    addError("Collection resolver '" + desired.key.resolver + "' returned misc asset id " + std::to_string(id.value) +
             (missing ? " that does not exist" : " that is not a misc asset"));
    desired.issues.push_back(CollectionIssue{
        .impact = CollectionIssueImpact::Incomplete,
        .severity = Severity::Error,
        .code = missing ? "missing-misc" : "wrong-type-misc",
        .message = "Collection references " + std::string(missing ? "missing" : "wrong-type") + " misc asset " +
                   std::to_string(id.value),
        .asset = id,
    });
    return true;
  });
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
