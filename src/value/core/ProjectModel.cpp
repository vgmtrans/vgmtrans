/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/ProjectModel.h"

#include <algorithm>
#include <variant>

namespace vgmtrans::core {

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

ItemNode* itemById(ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) {
    return item.id == id;
  });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

const ItemNode* itemById(const ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) {
    return item.id == id;
  });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

Asset* assetById(Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id;
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Asset* assetById(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id;
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Collection* collectionById(const Project& project, CollectionId id) {
  const auto found = std::ranges::find_if(project.collections, [id](const Collection& collection) {
    return collection.id == id;
  });
  if (found == project.collections.end()) {
    return nullptr;
  }
  return &*found;
}

}  // namespace vgmtrans::core
