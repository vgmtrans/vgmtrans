/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SourceInspection.h"

#include <algorithm>

namespace vgmtrans::core {

std::shared_ptr<const SourceInspection> SourceInspection::create(AssetMetadata metadata, const SourceMap& sourceMap,
                                                                 SharedSourceBytes sourceBytes) {
  if (sourceBytes == nullptr) {
    return {};
  }

  if (!metadata.range.valid() || metadata.range.offset > sourceBytes->size() ||
      metadata.range.size > sourceBytes->size() - metadata.range.offset) {
    return {};
  }

  const auto selected = sourceMap.annotationsForAsset(metadata.id);
  if (selected.empty()) {
    return {};
  }

  SourceRange inspectionRange = metadata.range;
  std::vector<SourceAnnotation> annotations;
  annotations.reserve(selected.size());
  for (const SourceAnnotationId id : selected) {
    const auto* annotation = sourceMap.find(id);
    if (annotation == nullptr) {
      continue;
    }
    if (annotation->range.source != inspectionRange.source || annotation->range.offset > sourceBytes->size() ||
        annotation->range.size > sourceBytes->size() - annotation->range.offset) {
      return {};
    }
    const u64 begin = std::min(inspectionRange.offset, annotation->range.offset);
    const u64 end = std::max(inspectionRange.endOffset(), annotation->range.endOffset());
    inspectionRange = SourceRange{.source = inspectionRange.source, .offset = begin, .size = end - begin};
    annotations.push_back(*annotation);
  }

  return std::shared_ptr<const SourceInspection>(new SourceInspection(
      std::move(metadata), inspectionRange, std::move(sourceBytes), SourceMap{std::move(annotations)}));
}

SourceInspection::SourceInspection(AssetMetadata metadata, SourceRange range, SharedSourceBytes sourceBytes,
                                   SourceMap sourceMap)
    : metadata_(std::move(metadata)), range_(range), sourceBytes_(std::move(sourceBytes)),
      sourceMap_(std::move(sourceMap)) {
  for (const auto& annotation : sourceMap_.annotations()) {
    if (!annotation.parent || sourceMap_.find(*annotation.parent) == nullptr) {
      roots_.push_back(annotation.id);
    }
  }
  sortBySourceOrder(roots_);
}

std::span<const u8> SourceInspection::bytes() const noexcept {
  if (sourceBytes_ == nullptr) {
    return {};
  }
  return std::span<const u8>{*sourceBytes_}.subspan(static_cast<size_t>(range_.offset),
                                                   static_cast<size_t>(range_.size));
}

const SourceAnnotation* SourceInspection::annotation(SourceAnnotationId id) const {
  return sourceMap_.find(id);
}

std::vector<SourceAnnotationId> SourceInspection::children(SourceAnnotationId parent) const {
  auto children = sourceMap_.childrenOf(parent);
  sortBySourceOrder(children);
  return children;
}

std::optional<SourceAnnotationId> SourceInspection::annotationAt(u64 offset) const {
  const SourceAnnotation* best = nullptr;
  size_t bestDepth = 0;

  for (const SourceAnnotationId id : sourceMap_.at(range_.source, offset)) {
    const auto* candidate = sourceMap_.find(id);
    if (candidate == nullptr) {
      continue;
    }

    size_t depth = 0;
    auto parent = candidate->parent;
    while (parent) {
      const auto* ancestor = sourceMap_.find(*parent);
      if (ancestor == nullptr) {
        break;
      }
      ++depth;
      parent = ancestor->parent;
    }

    if (best == nullptr || candidate->range.size < best->range.size ||
        (candidate->range.size == best->range.size && depth > bestDepth)) {
      best = candidate;
      bestDepth = depth;
    }
  }
  return best != nullptr ? std::optional{best->id} : std::nullopt;
}

void SourceInspection::sortBySourceOrder(std::vector<SourceAnnotationId>& annotations) const {
  std::ranges::sort(annotations, [this](SourceAnnotationId lhs, SourceAnnotationId rhs) {
    const auto* left = annotation(lhs);
    const auto* right = annotation(rhs);
    if (left == nullptr || right == nullptr) {
      return lhs.value < rhs.value;
    }
    if (left->range.offset != right->range.offset) {
      return left->range.offset < right->range.offset;
    }
    if (left->range.size != right->range.size) {
      return left->range.size > right->range.size;
    }
    return lhs.value < rhs.value;
  });
}

}  // namespace vgmtrans::core
