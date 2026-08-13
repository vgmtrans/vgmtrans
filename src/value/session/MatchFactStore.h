/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MatchModel.h"

#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Owns the facts that collection resolvers use to connect assets. Facts are removed
// either because they came from a deleted source or because they point at an asset
// that was removed with that source.
class MatchFactStore {
public:
  [[nodiscard]] const std::vector<MatchFact>& all() const noexcept { return facts_; }

  void append(std::vector<MatchFact> facts);
  void removeForSourcesAndAssets(const std::vector<SourceId>& sources, const std::unordered_set<u32>& assetIds);

private:
  std::vector<MatchFact> facts_;
};

}  // namespace vgmtrans::core
