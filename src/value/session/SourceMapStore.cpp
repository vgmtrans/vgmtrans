/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SourceMapStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

void validateUnique(const std::vector<SourceAnnotation>& target, const SourceMap& sourceMap) {
  std::unordered_set<u32> ids;
  ids.reserve(target.size() + sourceMap.annotations().size());
  for (const auto& annotation : target) {
    if (annotation.id.valid()) {
      ids.insert(annotation.id.value);
    }
  }
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.id.valid() && !ids.insert(annotation.id.value).second) {
      throw std::invalid_argument("Source map reused existing annotation id " + std::to_string(annotation.id.value));
    }
  }
}

void appendUnique(std::vector<SourceAnnotation>& target, const SourceMap& sourceMap) {
  validateUnique(target, sourceMap);
  const auto annotations = sourceMap.annotations();
  target.insert(target.end(), annotations.begin(), annotations.end());
}

}  // namespace

void SourceMapStore::validateAppend(const SourceMap& sourceMap) const {
  validateUnique(annotations_, sourceMap);
}

void SourceMapStore::append(SourceMap sourceMap) {
  appendUnique(annotations_, sourceMap);
}

std::unordered_set<u32> SourceMapStore::removeForAssets(const std::unordered_set<u32>& assetIds) {
  std::unordered_set<u32> removedAnnotations;
  if (assetIds.empty()) {
    return removedAnnotations;
  }

  for (const auto& annotation : annotations_) {
    if (annotation.owner && annotation.owner->asset.valid() && assetIds.contains(annotation.owner->asset.value)) {
      removedAnnotations.insert(annotation.id.value);
    }
  }

  // Unowned structural children belong to their nearest owned ancestor. Keep
  // an explicitly owned child from another asset and detach it instead.
  bool foundDescendant = true;
  while (foundDescendant) {
    foundDescendant = false;
    for (auto& annotation : annotations_) {
      if (removedAnnotations.contains(annotation.id.value) || !annotation.parent ||
          !removedAnnotations.contains(annotation.parent->value)) {
        continue;
      }
      if (annotation.owner && annotation.owner->asset.valid() &&
          !assetIds.contains(annotation.owner->asset.value)) {
        annotation.parent.reset();
        continue;
      }
      foundDescendant |= removedAnnotations.insert(annotation.id.value).second;
    }
  }

  for (auto& annotation : annotations_) {
    if (annotation.parent && removedAnnotations.contains(annotation.parent->value)) {
      annotation.parent.reset();
    }
    std::erase_if(annotation.links, [&](const SourceLink& link) {
      if (const auto* target = std::get_if<SourceAnnotationId>(&link.target)) {
        return removedAnnotations.contains(target->value);
      }
      if (const auto* target = std::get_if<ObjectRef>(&link.target)) {
        return target->asset.valid() && assetIds.contains(target->asset.value);
      }
      return false;
    });
  }

  std::erase_if(annotations_, [&](const SourceAnnotation& annotation) {
    return removedAnnotations.contains(annotation.id.value);
  });
  return removedAnnotations;
}

void SourceMapStore::removeForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = makeSourceIdSet(sources);
  std::erase_if(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.range.valid() && sourceIds.contains(annotation.range.source);
  });
}

SourceMap SourceMapStore::all() const {
  return SourceMap{annotations_};
}

}  // namespace vgmtrans::core
