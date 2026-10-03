/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SourceInspection.h"

#include <algorithm>

namespace vgmtrans::core {

SourceInspection::SourceInspection(AssetMetadata metadata, const SourceMap& sourceMap, SharedSourceBytes sourceBytes)
    : metadata_(std::move(metadata)), range_(metadata_.range), sourceBytes_(std::move(sourceBytes)) {
  std::vector<SourceAnnotation> selected;
  const auto annotations = sourceMap.annotationsForAsset(metadata_.id);
  selected.reserve(annotations.size());
  for (const SourceAnnotationId annotation : annotations) {
    selected.push_back(sourceMap.get(annotation));
  }
  sourceMap_ = SourceMap{std::move(selected)};

  for (const auto& annotation : sourceMap_.annotations()) {
    range_.include(annotation.range);
    if (!annotation.parent) {
      roots_.push_back(annotation.id);
    }
  }
  sortBySourceOrder(roots_);
}

std::span<const u8> SourceInspection::bytes() const noexcept {
  return std::span<const u8>{*sourceBytes_}.subspan(static_cast<size_t>(range_.offset),
                                                 static_cast<size_t>(range_.size));
}

const SourceAnnotation* SourceInspection::annotation(SourceAnnotationId id) const {
  return sourceMap_.find(id);
}

const SourceAnnotation* SourceInspection::annotation(SourceInspectionItem item) const {
  return annotation(item.annotation);
}

const SourceField* SourceInspection::field(SourceInspectionItem item) const {
  if (!item.field) {
    return nullptr;
  }
  const auto* owner = annotation(item.annotation);
  if (owner == nullptr || *item.field >= owner->fields.size()) {
    return nullptr;
  }
  return &owner->fields[*item.field];
}

SourceRange SourceInspection::range(SourceInspectionItem item) const {
  if (item.isField()) {
    const auto* projectedField = field(item);
    return projectedField != nullptr ? projectedField->range : SourceRange{};
  }
  const auto* owner = annotation(item.annotation);
  return owner != nullptr ? owner->range : SourceRange{};
}

std::vector<SourceAnnotationId> SourceInspection::children(SourceAnnotationId parent) const {
  auto children = sourceMap_.childrenOf(parent);
  sortBySourceOrder(children);
  return children;
}

std::optional<SourceInspectionItem> SourceInspection::itemAt(u64 offset) const {
  std::optional<SourceInspectionItem> best;
  SourceRange bestRange;
  size_t bestDepth = 0;

  const auto consider = [&](SourceInspectionItem item, SourceRange candidateRange, size_t depth) {
    if (!candidateRange.containsOffset(range_.source, offset)) {
      return;
    }
    if (!best || candidateRange.size < bestRange.size ||
        (candidateRange.size == bestRange.size && depth > bestDepth)) {
      best = item;
      bestRange = candidateRange;
      bestDepth = depth;
    }
  };

  for (const SourceAnnotation& candidate : sourceMap_.annotations()) {
    size_t depth = 0;
    auto parent = candidate.parent;
    while (parent) {
      ++depth;
      parent = sourceMap_.get(*parent).parent;
    }

    consider(SourceInspectionItem::forAnnotation(candidate.id), candidate.range, depth);
    if (!candidate.fieldsAsChildren) {
      continue;
    }
    for (u32 fieldIndex = 0; fieldIndex < candidate.fields.size(); ++fieldIndex) {
      consider(SourceInspectionItem::forField(candidate.id, fieldIndex),
               candidate.fields[fieldIndex].range, depth + 1);
    }
  }
  return best;
}

void SourceInspection::sortBySourceOrder(std::vector<SourceAnnotationId>& annotations) const {
  std::ranges::sort(annotations, [this](SourceAnnotationId lhs, SourceAnnotationId rhs) {
    const auto& left = sourceMap_.get(lhs);
    const auto& right = sourceMap_.get(rhs);
    if (left.range.offset != right.range.offset) {
      return left.range.offset < right.range.offset;
    }
    if (left.range.size != right.range.size) {
      return left.range.size > right.range.size;
    }
    return lhs.value < rhs.value;
  });
}

}  // namespace vgmtrans::core
