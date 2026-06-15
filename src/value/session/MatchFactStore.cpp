/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/MatchFactStore.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::unordered_set<u32> sourceIdSet(const std::vector<SourceId>& sources) {
  std::unordered_set<u32> ids;
  ids.reserve(sources.size());
  for (const SourceId source : sources) {
    ids.insert(source.value);
  }
  return ids;
}

[[nodiscard]] bool factFromSource(const MatchFact& fact, const std::unordered_set<u32>& sourceIds) {
  return fact.scope.source && sourceIds.contains(fact.scope.source->value);
}

}  // namespace

void MatchFactStore::append(std::vector<MatchFact> facts) {
  facts_.insert(facts_.end(), std::make_move_iterator(facts.begin()), std::make_move_iterator(facts.end()));
}

void MatchFactStore::removeForSourcesAndAssets(const std::vector<SourceId>& sources,
                                               const std::unordered_set<u32>& assetIds) {
  const auto sourceIds = sourceIdSet(sources);
  std::erase_if(facts_, [&](const MatchFact& fact) {
    return factFromSource(fact, sourceIds) || (fact.asset.valid() && assetIds.contains(fact.asset.value));
  });
}

}  // namespace vgmtrans::core
