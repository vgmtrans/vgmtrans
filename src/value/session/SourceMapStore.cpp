/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/SourceMapStore.h"

#include "value/session/SourceIdSet.h"

#include <algorithm>

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

SourceMap SourceMapStore::all() const {
  return SourceMap{annotations_};
}

}  // namespace vgmtrans::core
