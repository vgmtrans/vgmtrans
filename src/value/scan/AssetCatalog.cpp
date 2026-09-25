/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/AssetCatalog.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

AssetCatalog::AssetCatalog(std::vector<SourceFile> sources, SharedSequence<Asset> assets)
    : sources_(std::move(sources)), assets_(std::move(assets)) {
  for (size_t index = 0; index < sources_.size(); ++index) {
    sourcesById_.emplace(sources_[index].id.value, index);
  }
  assetsById_.reserve(assets_.size());
  for (const auto& asset : assets_) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, &asset);
    }
  }
}

const Asset* AssetCatalog::asset(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }
  const auto found = assetsById_.find(id.value);
  return found != assetsById_.end() ? found->second : nullptr;
}

SourceId AssetCatalog::sourceRoot(SourceId source) const noexcept {
  // Session admission prevents cycles; the bound also protects hand-built snapshots.
  for (size_t depth = 0; depth < sources_.size(); ++depth) {
    const auto found = sourcesById_.find(source.value);
    if (found == sourcesById_.end() || !sources_[found->second].parent) {
      return source;
    }
    source = *sources_[found->second].parent;
  }
  return source;
}

const SourceFile* AssetCatalog::sourceFor(const AssetMetadata& metadata) const noexcept {
  const auto found = sourcesById_.find(metadata.range.source.value);
  return found != sourcesById_.end() ? &sources_[found->second] : nullptr;
}

}  // namespace vgmtrans::core
