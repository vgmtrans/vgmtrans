/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_map>

namespace vgmtrans::core {

namespace {

void normalizeItemTree(ItemTree& items, ScanIdAllocator& ids) {
  // Scanners may build item trees manually. Assign any missing IDs and rebuild
  // child lists before the tree is published.
  for (auto& item : items.nodes) {
    if (item.id.valid()) {
      ids.reserveAfter(item.id);
    } else {
      item.id = ids.nextItemId();
    }
    item.children.clear();
  }

  if (items.nodes.empty()) {
    items.root = std::nullopt;
    return;
  }

  std::unordered_map<u32, std::size_t> itemIndexes;
  itemIndexes.reserve(items.nodes.size());
  for (std::size_t i = 0; i < items.nodes.size(); ++i) {
    itemIndexes.emplace(items.nodes[i].id.value, i);
  }

  const auto itemByIndexedId = [&items, &itemIndexes](ItemId id) -> ItemNode* {
    const auto found = itemIndexes.find(id.value);
    if (found == itemIndexes.end() || found->second >= items.nodes.size()) {
      return nullptr;
    }

    auto& item = items.nodes[found->second];
    return item.id == id ? &item : nullptr;
  };

  std::optional<ItemId> firstRoot;
  for (auto& item : items.nodes) {
    if (!item.parent.has_value()) {
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
      continue;
    }

    if (auto* parent = itemByIndexedId(*item.parent)) {
      parent->children.push_back(item.id);
    } else {
      // Keep orphaned nodes inspectable instead of dropping source context.
      item.parent = std::nullopt;
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
    }
  }

  if (!items.root.has_value() || !itemByIndexedId(*items.root)) {
    items.root = firstRoot;
  }
}

void assignMissingAssetIds(std::vector<Asset>& assets, ScanIdAllocator& ids) {
  // Formats may assign IDs when assets reference each other. If they leave an ID
  // empty, assign one here before the asset leaves the scan result.
  for (auto& asset : assets) {
    auto& meta = metadata(asset);
    if (meta.id.valid()) {
      ids.reserveAfter(meta.id);
    } else {
      meta.id = ids.nextAssetId();
    }
    normalizeItemTree(meta.items, ids);
  }
}

}  // namespace

void normalizeScanResult(ScanResult& result, ScanIdAllocator& ids) {
  assignMissingAssetIds(result.assets, ids);
}

}  // namespace vgmtrans::core
