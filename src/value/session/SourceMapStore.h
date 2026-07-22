/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SourceMap.h"

#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Owns source annotations produced by scans. SourceMap itself is immutable once
// built, so the store keeps raw annotations and publishes a fresh map for each
// snapshot.
class SourceMapStore {
public:
  void validateAppend(const SourceMap& sourceMap) const;
  void append(SourceMap sourceMap);
  [[nodiscard]] std::unordered_set<u32> removeForAssets(const std::unordered_set<u32>& assetIds);
  void removeForSources(const std::vector<SourceId>& sources);

  [[nodiscard]] SourceMap all() const;

private:
  std::vector<SourceAnnotation> annotations_;
};

}  // namespace vgmtrans::core
