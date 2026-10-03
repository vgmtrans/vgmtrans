/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SessionState.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::shared_ptr<const std::vector<Asset>> sharedAssets(std::vector<Asset> values) {
  if (values.empty()) {
    static const auto empty = std::make_shared<const std::vector<Asset>>();
    return empty;
  }
  return std::make_shared<const std::vector<Asset>>(std::move(values));
}

template <typename Predicate>
[[nodiscard]] std::shared_ptr<const std::vector<Asset>> without(const std::shared_ptr<const std::vector<Asset>>& values,
                                                              Predicate remove) {
  if (!std::ranges::any_of(*values, remove)) {
    return values;
  }
  std::vector<Asset> filtered;
  filtered.reserve(values->size());
  for (const auto& value : *values) {
    if (!remove(value)) {
      filtered.push_back(value);
    }
  }
  return sharedAssets(std::move(filtered));
}

[[nodiscard]] bool referencesAnyAsset(const CollectionMembers& members, const std::unordered_set<AssetId>& assetIds) {
  const auto matches = [&](AssetId id) { return assetIds.contains(id); };
  return (members.sequence && matches(*members.sequence)) || std::ranges::any_of(members.soundBanks, matches) ||
         std::ranges::any_of(members.samplePools, matches) || std::ranges::any_of(members.miscAssets, matches);
}

}  // namespace

const Asset* SessionState::asset(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }
  const auto found = assetsById_.find(id);
  return found != assetsById_.end() ? found->second : nullptr;
}

void SessionState::appendScan(ScanResult result) {
  // validateScanResult() has already admitted asset IDs. Annotation IDs need a
  // separate cross-scan check because validation does not see session annotations.
  for (const auto& annotation : result.sourceMap.annotations()) {
    if (annotation.id.valid() && annotationIds_.contains(annotation.id)) {
      throw std::invalid_argument("Source map reused existing annotation id " + std::to_string(annotation.id.value));
    }
  }

  if (!result.assets.empty() || !result.sourceMap.empty()) {
    scanChunks_.push_back(ScanChunk{
        .assets = sharedAssets(std::move(result.assets)),
        .sourceMap = std::move(result.sourceMap),
    });
    const auto& chunk = scanChunks_.back();

    for (const auto& value : *chunk.assets) {
      assetsById_.emplace(metadata(value).id, &value);
    }
    for (const auto& annotation : chunk.sourceMap.annotations()) {
      if (annotation.id.valid()) {
        annotationIds_.insert(annotation.id);
      }
    }
    rebuildViews();
  }

  addDiagnostics(std::move(result.diagnostics));
}

void SessionState::removeAssets(std::span<const AssetId> assets) {
  std::unordered_set<AssetId> removed;
  removed.reserve(assets.size());
  for (const AssetId id : assets) {
    if (asset(id) != nullptr) {
      removed.insert(id);
    }
  }
  if (!removed.empty()) {
    removeDiscoveredData({}, removed);
  }
}

void SessionState::removeSources(std::span<const SourceId> sources) {
  std::unordered_set<SourceId> sourceIds;
  sourceIds.reserve(sources.size());
  for (const SourceId source : sources) {
    if (source.valid()) {
      sourceIds.insert(source);
    }
  }

  std::unordered_set<AssetId> removedAssets;
  for (const auto& value : assets_) {
    const auto& meta = metadata(value);
    if (meta.id.valid() && meta.range.valid() && sourceIds.contains(meta.range.source)) {
      removedAssets.insert(meta.id);
    }
  }
  removeDiscoveredData(sourceIds, removedAssets);
}

CollectionId SessionState::createUserCollection(Collection collection) {
  const auto& name = collection.name;
  const auto& members = collection.selection;
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
    std::unordered_set<AssetId> seen;
    for (const AssetId id : values) {
      if (!seen.insert(id).second) {
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
  collection.id = id;
  collection.origin = CollectionOrigin::User;
  collections_.push_back(std::move(collection));
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

// Banks and samples can change as files arrive or are removed. Keep a discovered
// collection's ID while its sequence stays the same; leave user collections alone.
void SessionState::reconcileCollections(std::vector<Collection> desired) {
  std::unordered_set<AssetId> sequences;
  for (auto& collection : desired) {
    const AssetId sequence = collection.selection.sequence.value();
    sequences.insert(sequence);
    validateMiscAssets(collection);
    collection.origin = CollectionOrigin::Discovered;
    const auto found = std::ranges::find_if(collections_, [&](const Collection& existing) {
      return existing.isDiscovered() && existing.selection.sequence == sequence;
    });
    if (found != collections_.end()) {
      collection.id = found->id;
      *found = std::move(collection);
    } else {
      collection.id = CollectionId{nextCollectionId_++};
      collections_.push_back(std::move(collection));
    }
  }

  std::erase_if(collections_, [&](const Collection& collection) {
    return collection.isDiscovered() && !sequences.contains(collection.selection.sequence.value());
  });
}

void SessionState::removeDiscoveredData(const std::unordered_set<SourceId>& sourceIds,
                                        const std::unordered_set<AssetId>& assetIds) {
  if (sourceIds.empty() && assetIds.empty()) {
    return;
  }

  std::unordered_set<SourceAnnotationId> removedAnnotations;
  for (const SourceId source : sourceIds) {
    for (const SourceAnnotationId annotation : sourceMap_.annotationsForSource(source)) {
      removedAnnotations.insert(annotation);
    }
  }
  for (const AssetId asset : assetIds) {
    for (const SourceAnnotationId annotation : sourceMap_.annotationsForAsset(asset)) {
      removedAnnotations.insert(annotation);
    }
  }

  const auto removesAsset = [&](const Asset& value) {
    const AssetId id = metadata(value).id;
    return id.valid() && assetIds.contains(id);
  };
  const auto removesLink = [&](const SourceLink& link) {
    if (const auto* target = std::get_if<SourceRange>(&link.target)) {
      return target->valid() && sourceIds.contains(target->source);
    }
    if (const auto* target = std::get_if<SourceAnnotationId>(&link.target)) {
      return removedAnnotations.contains(*target);
    }
    const auto* target = std::get_if<ObjectRef>(&link.target);
    return target != nullptr && target->asset.valid() && assetIds.contains(target->asset);
  };
  const auto removesDiagnostic = [&](const Diagnostic& diagnostic) {
    const bool removedSource = diagnostic.range.valid() && sourceIds.contains(diagnostic.range.source);
    const bool removedObject =
        diagnostic.object && diagnostic.object->asset.valid() && assetIds.contains(diagnostic.object->asset);
    const bool removedAnnotation = diagnostic.annotation && removedAnnotations.contains(*diagnostic.annotation);
    return removedSource || removedObject || removedAnnotation;
  };

  for (auto& chunk : scanChunks_) {
    chunk.assets = without(chunk.assets, removesAsset);

    if (std::ranges::any_of(chunk.sourceMap.annotations(), [&](const SourceAnnotation& annotation) {
          return removedAnnotations.contains(annotation.id) ||
                 (annotation.parent && removedAnnotations.contains(*annotation.parent)) ||
                 std::ranges::any_of(annotation.links, removesLink);
        })) {
      std::vector<SourceAnnotation> annotations;
      annotations.reserve(chunk.sourceMap.annotations().size());
      for (const auto& value : chunk.sourceMap.annotations()) {
        if (removedAnnotations.contains(value.id)) {
          continue;
        }
        auto annotation = value;
        if (annotation.parent && removedAnnotations.contains(*annotation.parent)) {
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
    return !collection.isDiscovered() && referencesAnyAsset(collection.members(), assetIds);
  });
  rebuildViews();
  rebuildIndexes();
}

void SessionState::validateMiscAssets(Collection& desired) {
  // Audio inputs were checked during resolution. These extra assets are for
  // inspection only, so losing one reports a problem without blocking audio preparation.
  if (desired.selection.miscAssets.empty()) {
    return;
  }
  std::erase_if(desired.selection.miscAssets, [&](AssetId id) {
    if (asset<MiscAsset>(id) != nullptr) {
      return false;
    }
    desired.inputs.inspectionStatus = ResolutionStatus::Incomplete;
    const bool missing = asset(id) == nullptr;
    addError("Collection '" + desired.name + "' references misc asset id " + std::to_string(id.value) +
             (missing ? " that does not exist" : " that is not a misc asset"));
    desired.issues.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = missing ? "missing-misc" : "wrong-type-misc",
        .message = "Collection references " + std::string(missing ? "missing" : "wrong-type") + " misc asset " +
                   std::to_string(id.value),
        .object = ObjectRefs::asset(id),
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

  SharedSequence<Asset> assets{std::move(assetChunks)};
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
      assetsById_.emplace(id, &asset);
    }
  }

  annotationIds_.clear();
  annotationIds_.reserve(sourceMap_.annotations().size());
  for (const auto& annotation : sourceMap_.annotations()) {
    if (annotation.id.valid()) {
      annotationIds_.insert(annotation.id);
    }
  }
}

}  // namespace vgmtrans::core
