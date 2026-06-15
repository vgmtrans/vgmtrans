/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

#include <algorithm>
#include <optional>

namespace vgmtrans::core {

namespace {

void normalizeItemTree(ItemTree& items, ScanIdAllocator& ids) {
  // Modules can return hand-authored or builder-created trees; normalize them before publishing.
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

  std::optional<ItemId> firstRoot;
  for (auto& item : items.nodes) {
    if (!item.parent.has_value()) {
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
      continue;
    }

    if (auto* parent = itemById(items, *item.parent)) {
      parent->children.push_back(item.id);
    } else {
      // Keep orphaned nodes inspectable instead of dropping source context.
      item.parent = std::nullopt;
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
    }
  }

  if (!items.root.has_value() || !itemById(items, *items.root)) {
    items.root = firstRoot;
  }
}

void assignMissingAssetIds(std::vector<Asset>& assets, ScanIdAllocator& ids) {
  // Format modules may assign IDs when cross-references are known during parsing. If
  // they do not, the shared scanner fills them in consistently before publishing assets.
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
