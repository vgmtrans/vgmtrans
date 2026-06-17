/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"

#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Owns collections that scanners already know how to assemble. This is separate
// from MatchFactStore because no resolver judgment is needed for these groups.
class ExplicitCollectionStore {
public:
  void append(std::vector<ExplicitCollection> collections, SourceId owner);
  void removeForSourcesAndAssets(const std::vector<SourceId>& sources, const std::unordered_set<u32>& assetIds);

  [[nodiscard]] std::map<std::string, std::vector<DesiredCollection>> desiredByResolver() const;

private:
  struct Entry {
    SourceId owner;
    ExplicitCollection collection;
  };

  std::vector<Entry> entries_;
  std::set<std::string> knownResolvers_;
};

}  // namespace vgmtrans::core
