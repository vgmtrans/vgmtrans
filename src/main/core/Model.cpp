/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/Model.h"

namespace vgmtrans::core {

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

SourceRange commandRange(const SequencerCommand& command) {
  return std::visit([](const auto& typedCommand) { return typedCommand.range; }, command);
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
