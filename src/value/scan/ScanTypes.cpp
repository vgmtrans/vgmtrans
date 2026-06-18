/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace vgmtrans::core {

AssetId ScanIdAllocator::nextAssetId() noexcept {
  return AssetId{nextAssetId_++};
}

CollectionId ScanIdAllocator::nextCollectionId() noexcept {
  return CollectionId{nextCollectionId_++};
}

ItemId ScanIdAllocator::nextItemId() noexcept {
  return ItemId{nextItemId_++};
}

SourceAnnotationId ScanIdAllocator::nextSourceAnnotationId() noexcept {
  return SourceAnnotationId{nextSourceAnnotationId_++};
}

void ScanIdAllocator::reserveAfter(AssetId id) noexcept {
  if (id.valid()) {
    nextAssetId_ = std::max(nextAssetId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(CollectionId id) noexcept {
  if (id.valid()) {
    nextCollectionId_ = std::max(nextCollectionId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(ItemId id) noexcept {
  if (id.valid()) {
    nextItemId_ = std::max(nextItemId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(SourceAnnotationId id) noexcept {
  if (id.valid()) {
    nextSourceAnnotationId_ = std::max(nextSourceAnnotationId_, id.value + 1);
  }
}

ItemTreeBuilder::ItemTreeBuilder(ItemTree& tree, ScanIdAllocator& ids) : tree_(tree), ids_(ids) {
  itemIndexes_.reserve(tree_.nodes.size());
  for (std::size_t i = 0; i < tree_.nodes.size(); ++i) {
    const ItemId id = tree_.nodes[i].id;
    if (id.valid()) {
      ids_.reserveAfter(id);
      itemIndexes_.emplace(id.value, i);
    }
  }
}

ItemId ItemTreeBuilder::add(std::optional<ItemId> parent, ItemKind kind, std::string detailKind, std::string name,
                            SourceRange range, std::string description) {
  const auto id = ids_.nextItemId();
  const auto itemIndex = tree_.nodes.size();
  tree_.nodes.push_back(ItemNode{
      .id = id,
      .parent = parent,
      .kind = kind,
      .detailKind = std::move(detailKind),
      .name = std::move(name),
      .description = std::move(description),
      .range = range,
  });
  itemIndexes_.emplace(id.value, itemIndex);
  if (parent) {
    if (auto* parentItem = item(*parent)) {
      parentItem->children.push_back(id);
    }
  } else {
    tree_.root = id;
  }
  return id;
}

ItemNode* ItemTreeBuilder::item(ItemId id) {
  const auto found = itemIndexes_.find(id.value);
  if (found == itemIndexes_.end() || found->second >= tree_.nodes.size()) {
    return nullptr;
  }

  auto& item = tree_.nodes[found->second];
  return item.id == id ? &item : nullptr;
}

}  // namespace vgmtrans::core
