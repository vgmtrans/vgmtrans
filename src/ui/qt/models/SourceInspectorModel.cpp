/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "models/SourceInspectorModel.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace vgmtrans::ui {

namespace {

bool intersects(core::SourceRange lhs, core::SourceRange rhs) {
  if (!lhs.valid() || !rhs.valid() || lhs.source != rhs.source) {
    return false;
  }
  if (lhs.size == 0) {
    return rhs.size == 0 ? lhs.offset == rhs.offset : rhs.offset <= lhs.offset && lhs.offset < rhs.endOffset();
  }
  if (rhs.size == 0) {
    return lhs.offset <= rhs.offset && rhs.offset < lhs.endOffset();
  }
  return lhs.offset < rhs.endOffset() && rhs.offset < lhs.endOffset();
}

bool containsOffset(core::SourceRange range, u64 offset) {
  if (range.size == 0) {
    return range.offset == offset;
  }
  return range.offset <= offset && offset < range.endOffset();
}

}  // namespace

SourceInspectorModel::SourceInspectorModel(const WorkspaceController& workspace, core::AssetId asset) : asset_(asset) {
  const auto* value = workspace.snapshot().asset(asset);
  if (value == nullptr) {
    return;
  }

  metadata_ = core::metadata(*value);
  if (!metadata_.range.valid()) {
    return;
  }

  const auto sourceBytes = workspace.sourceBytes(metadata_.range.source);
  if (metadata_.range.offset > sourceBytes.size()) {
    return;
  }

  const auto& sourceMap = workspace.snapshot().sourceMap();
  std::unordered_set<u32> selected;
  std::unordered_set<u32> coverage;
  std::vector<core::SourceAnnotationId> anchors;

  // ObjectRef kinds all carry their owning asset when the annotation belongs to
  // a published asset. These anchors let us distinguish sibling assets even
  // when their data lives in the same source.
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.owner && annotation.owner->asset == asset_) {
      anchors.push_back(annotation.id);
    }
  }

  const auto includeDescendants = [&](std::span<const core::SourceAnnotationId> roots) {
    std::vector<core::SourceAnnotationId> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
      const core::SourceAnnotationId id = pending.back();
      pending.pop_back();
      if (!id.valid() || !selected.insert(id.value).second) {
        continue;
      }
      const auto* annotation = sourceMap.find(id);
      if (annotation == nullptr) {
        continue;
      }
      coverage.insert(id.value);
      const auto children = sourceMap.childrenOf(id);
      pending.insert(pending.end(), children.begin(), children.end());
    }
  };

  includeDescendants(anchors);
  const bool hasAssetAnchors = !selected.empty();

  // A format may initially expose ranges before it has added object ownership.
  // In that case the metadata span is the best available root boundary; still
  // follow its descendants because decoded sequence tracks may be noncontiguous
  // with the header.
  if (!hasAssetAnchors) {
    std::vector<core::SourceAnnotationId> rangeAnchors;
    for (const auto& annotation : sourceMap.annotations()) {
      if (annotation.range.source == metadata_.range.source && intersects(annotation.range, metadata_.range)) {
        rangeAnchors.push_back(annotation.id);
      }
    }
    includeDescendants(rangeAnchors);
  }

  // Retain ancestors for the tree hierarchy, but do not let an unowned parent
  // expand the byte view. A container annotation can span several sibling
  // assets in one source.
  std::vector<core::SourceAnnotationId> pendingParents;
  pendingParents.reserve(selected.size());
  for (const u32 id : selected) {
    pendingParents.push_back(core::SourceAnnotationId{id});
  }
  while (!pendingParents.empty()) {
    const auto* annotation = sourceMap.find(pendingParents.back());
    pendingParents.pop_back();
    if (annotation == nullptr || !annotation->parent || !annotation->parent->valid()) {
      continue;
    }
    if (selected.insert(annotation->parent->value).second) {
      pendingParents.push_back(*annotation->parent);
    }
  }

  // Asset metadata is often only a sequence header or pointer table. Expand
  // the inspector to the bounded union of owned annotations and their decoded
  // descendants so track data remains visible without changing asset identity.
  const u64 sourceSize = static_cast<u64>(sourceBytes.size());
  u64 viewBegin = metadata_.range.offset;
  u64 viewEnd = viewBegin + std::min(metadata_.range.size, sourceSize - viewBegin);
  for (const u32 id : coverage) {
    const auto* annotation = sourceMap.find(core::SourceAnnotationId{id});
    if (annotation == nullptr || annotation->range.source != metadata_.range.source ||
        annotation->range.offset > sourceSize) {
      continue;
    }
    const u64 end = annotation->range.offset + std::min(annotation->range.size, sourceSize - annotation->range.offset);
    viewBegin = std::min(viewBegin, annotation->range.offset);
    viewEnd = std::max(viewEnd, end);
  }

  const u64 byteCount = viewEnd - viewBegin;
  if (byteCount > static_cast<u64>(std::numeric_limits<size_t>::max()) || viewBegin > std::numeric_limits<u32>::max() ||
      byteCount > std::numeric_limits<u32>::max() - viewBegin) {
    return;
  }

  range_ = core::SourceRange{.source = metadata_.range.source, .offset = viewBegin, .size = byteCount};
  const auto begin = sourceBytes.begin() + static_cast<size_t>(range_.offset);
  bytes_.assign(begin, begin + static_cast<size_t>(range_.size));

  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.range.source != range_.source || !intersects(annotation.range, range_)) {
      continue;
    }
    if (!selected.empty() && !selected.contains(annotation.id.value)) {
      continue;
    }
    annotations_.push_back(annotation);
  }

  // Completely unannotated assets still get a useful raw-byte inspector.
  if (annotations_.empty() && selected.empty()) {
    for (const auto& annotation : sourceMap.annotations()) {
      if (annotation.range.source == range_.source && intersects(annotation.range, range_)) {
        annotations_.push_back(annotation);
      }
    }
  }

  buildHierarchy();
  valid_ = true;
}

const core::SourceAnnotation* SourceInspectorModel::annotation(core::SourceAnnotationId id) const {
  const auto found = annotationIndexes_.find(id.value);
  if (found == annotationIndexes_.end() || found->second >= annotations_.size()) {
    return nullptr;
  }
  return &annotations_[found->second];
}

std::span<const core::SourceAnnotationId> SourceInspectorModel::children(core::SourceAnnotationId parent) const {
  const auto found = children_.find(parent.value);
  return found == children_.end() ? std::span<const core::SourceAnnotationId>{}
                                  : std::span<const core::SourceAnnotationId>{found->second};
}

std::optional<core::SourceAnnotationId> SourceInspectorModel::annotationAt(u64 offset) const {
  const core::SourceAnnotation* best = nullptr;
  size_t bestDepth = 0;

  for (const auto& candidate : annotations_) {
    if (!containsOffset(candidate.range, offset)) {
      continue;
    }

    size_t depth = 0;
    auto parent = candidate.parent;
    std::unordered_set<u32> visited;
    while (parent && visited.insert(parent->value).second) {
      ++depth;
      const auto* value = annotation(*parent);
      parent = value != nullptr ? value->parent : std::nullopt;
    }

    if (best == nullptr || candidate.range.size < best->range.size ||
        (candidate.range.size == best->range.size && depth > bestDepth)) {
      best = &candidate;
      bestDepth = depth;
    }
  }
  return best != nullptr ? std::optional{best->id} : std::nullopt;
}

void SourceInspectorModel::buildHierarchy() {
  annotationIndexes_.reserve(annotations_.size());
  for (size_t index = 0; index < annotations_.size(); ++index) {
    annotationIndexes_.emplace(annotations_[index].id.value, index);
  }

  auto order = [this](core::SourceAnnotationId lhs, core::SourceAnnotationId rhs) {
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
    return left->id.value < right->id.value;
  };

  for (const auto& item : annotations_) {
    if (item.parent && annotation(*item.parent) != nullptr) {
      children_[item.parent->value].push_back(item.id);
    } else {
      roots_.push_back(item.id);
    }
  }
  std::ranges::sort(roots_, order);
  for (auto& [_, values] : children_) {
    std::ranges::sort(values, order);
  }
}

}  // namespace vgmtrans::ui
