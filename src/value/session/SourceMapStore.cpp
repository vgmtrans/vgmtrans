/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SourceMapStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>
#include <utility>

namespace vgmtrans::core {

void SourceMapStore::append(SourceMap sourceMap) {
  const auto annotations = sourceMap.annotations();
  annotations_.insert(annotations_.end(), annotations.begin(), annotations.end());
}

void SourceMapStore::removeForSources(const std::vector<SourceId>& sources) {
  const auto sourceIds = makeSourceIdSet(sources);
  std::erase_if(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.range.valid() && sourceIds.contains(annotation.range.source);
  });
}

void SourceMapStore::replaceForAssets(const std::vector<AssetId>& assets, SourceMap sourceMap) {
  std::unordered_set<u32> assetIds;
  assetIds.reserve(assets.size());
  for (const AssetId asset : assets) {
    if (asset.valid()) {
      assetIds.insert(asset.value);
    }
  }
  removeForAssets(assetIds);
  append(std::move(sourceMap));
}

void SourceMapStore::removeForAssets(const std::unordered_set<u32>& assets) {
  std::erase_if(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.owner && annotation.owner->asset.valid() && assets.contains(annotation.owner->asset.value);
  });
}

SourceMap SourceMapStore::all() const {
  return SourceMap{annotations_};
}

}  // namespace vgmtrans::core
