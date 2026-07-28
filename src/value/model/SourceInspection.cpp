/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SourceInspection.h"

#include <algorithm>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool containsOffset(SourceRange range, SourceId source, u64 offset) {
  if (!range.valid() || range.source != source) {
    return false;
  }
  return range.size == 0 ? range.offset == offset : range.offset <= offset && offset < range.endOffset();
}

}  // namespace

std::shared_ptr<const SourceInspection> SourceInspection::create(AssetMetadata metadata, SourceMap sourceMap,
                                                                 SharedSourceBytes sourceBytes) {
  if (sourceBytes == nullptr) {
    return {};
  }

  if (!metadata.range.valid() || metadata.range.offset > sourceBytes->size() ||
      metadata.range.size > sourceBytes->size() - metadata.range.offset) {
    return {};
  }

  SourceRange inspectionRange = metadata.range;
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.range.source != inspectionRange.source || annotation.range.offset > sourceBytes->size() ||
        annotation.range.size > sourceBytes->size() - annotation.range.offset) {
      return {};
    }
    const u64 begin = std::min(inspectionRange.offset, annotation.range.offset);
    const u64 end = std::max(inspectionRange.endOffset(), annotation.range.endOffset());
    inspectionRange = SourceRange{.source = inspectionRange.source, .offset = begin, .size = end - begin};
  }
  if (sourceMap.empty()) {
    return {};
  }

  return std::shared_ptr<const SourceInspection>(
      new SourceInspection(std::move(metadata), inspectionRange, std::move(sourceBytes), std::move(sourceMap)));
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

std::optional<SourceAnnotationId> SourceInspection::annotationAt(u64 offset) const {
  const auto item = itemAt(offset);
  return item ? std::optional{item->annotation} : std::nullopt;
}

std::optional<SourceInspectionItem> SourceInspection::itemAt(u64 offset) const {
  SourceInspectionItem best;
  SourceRange bestRange;
  size_t bestDepth = 0;

  for (const SourceAnnotation& candidateValue : sourceMap_.annotations()) {
    const auto* candidate = &candidateValue;
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

    if (containsOffset(candidate->range, range_.source, offset) &&
        (!best.valid() || candidate->range.size < bestRange.size ||
         (candidate->range.size == bestRange.size && depth > bestDepth))) {
      best = SourceInspectionItem::forAnnotation(candidate->id);
      bestRange = candidate->range;
      bestDepth = depth;
    }

    if (!candidate->fieldsAsChildren) {
      continue;
    }
    for (u32 fieldIndex = 0; fieldIndex < candidate->fields.size(); ++fieldIndex) {
      const SourceField& field = candidate->fields[fieldIndex];
      if (!containsOffset(field.range, range_.source, offset)) {
        continue;
      }
      const size_t fieldDepth = depth + 1;
      if (!best.valid() || field.range.size < bestRange.size ||
          (field.range.size == bestRange.size && fieldDepth > bestDepth)) {
        best = SourceInspectionItem::forField(candidate->id, fieldIndex);
        bestRange = field.range;
        bestDepth = fieldDepth;
      }
    }
  }
  return best.valid() ? std::optional{best} : std::nullopt;
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
