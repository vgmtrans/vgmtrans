/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionResolver.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

MatchFactIndex::MatchFactIndex(const MatchContext& context) : context_(context) {
  assetsById_.reserve(context.assets().size());
  for (const auto& asset : context.assets()) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, &asset);
    }
  }
}

const Asset* MatchFactIndex::asset(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }
  const auto found = assetsById_.find(id.value);
  return found != assetsById_.end() ? found->second : nullptr;
}

const SourceFile* MatchFactIndex::sourceFor(const MatchFact& fact) const {
  if (!fact.scope.source) {
    return nullptr;
  }
  return context_.sources().contains(*fact.scope.source) ? &context_.sources().source(*fact.scope.source) : nullptr;
}

CollectionAssembly::CollectionAssembly(CollectionKey key, std::string name)
    : collection_(DesiredCollection{
          .key = std::move(key),
          .name = std::move(name),
      }) {
}

CollectionAssembly& CollectionAssembly::sequence(AssetId id) {
  collection_.members.sequence = id;
  return *this;
}

CollectionAssembly& CollectionAssembly::soundBank(AssetId id) {
  addUnique(collection_.members.soundBanks, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::samplePool(AssetId id) {
  addUnique(collection_.members.samplePools, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::misc(AssetId id) {
  addUnique(collection_.members.miscAssets, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::issue(CollectionIssue issue) {
  collection_.issues.push_back(std::move(issue));
  return *this;
}

CollectionAssembly& CollectionAssembly::incomplete(CollectionIssue issue) {
  issue.impact = CollectionIssueImpact::Incomplete;
  collection_.issues.push_back(std::move(issue));
  return *this;
}

CollectionAssembly& CollectionAssembly::ambiguous(std::string message, std::optional<AssetId> asset,
                                                  std::optional<SourceRange> range) {
  collection_.issues.push_back(ambiguousMatchIssue(std::move(message), asset, range));
  return *this;
}

CollectionAssembly& CollectionAssembly::requireSequence() {
  if (!collection_.members.sequence) {
    incomplete(missingSequenceIssue());
  }
  return *this;
}

CollectionAssembly& CollectionAssembly::requireSoundBank() {
  if (collection_.members.soundBanks.empty()) {
    incomplete(missingSoundBankIssue());
  }
  return *this;
}

CollectionAssembly& CollectionAssembly::requireSamplePool() {
  if (collection_.members.samplePools.empty()) {
    incomplete(missingSamplePoolIssue());
  }
  return *this;
}

DesiredCollection CollectionAssembly::finish() && {
  return std::move(collection_);
}

void CollectionAssembly::addUnique(std::vector<AssetId>& ids, AssetId id) {
  if (std::ranges::find(ids, id) == ids.end()) {
    ids.push_back(id);
  }
}

}  // namespace vgmtrans::core
