/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/ScanTypes.h"

#include <algorithm>
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

ItemTreeBuilder::ItemTreeBuilder(ItemTree& tree, ScanIdAllocator& ids) : tree_(tree), ids_(ids) {
}

ItemId ItemTreeBuilder::add(
    std::optional<ItemId> parent,
    ItemKind kind,
    std::string detailKind,
    std::string name,
    SourceRange range,
    std::string description) {
  const auto id = ids_.nextItemId();
  tree_.nodes.push_back(ItemNode{
      .id = id,
      .parent = parent,
      .kind = kind,
      .detailKind = std::move(detailKind),
      .name = std::move(name),
      .description = std::move(description),
      .range = range,
  });
  if (parent) {
    if (auto* parentItem = itemById(tree_, *parent)) {
      parentItem->children.push_back(id);
    }
  } else {
    tree_.root = id;
  }
  return id;
}

}  // namespace vgmtrans::core
