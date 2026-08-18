/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionDiscovery.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

CollectionDiscoveryContext::CollectionDiscoveryContext(const SourceStore& sources, SharedSequence<Asset> assets)
    : sources_(sources), assets_(std::move(assets)) {
  assetsById_.reserve(assets_.size());
  for (const auto& asset : assets_) {
    const AssetId id = metadata(asset).id;
    if (id.valid()) {
      assetsById_.emplace(id.value, &asset);
    }
  }
}

const Asset* CollectionDiscoveryContext::asset(AssetId id) const noexcept {
  if (!id.valid()) {
    return nullptr;
  }
  const auto found = assetsById_.find(id.value);
  return found != assetsById_.end() ? found->second : nullptr;
}

const SourceFile* CollectionDiscoveryContext::sourceFor(const AssetMetadata& metadata) const noexcept {
  if (!metadata.range.valid() || !sources_.contains(metadata.range.source)) {
    return nullptr;
  }
  return &sources_.source(metadata.range.source);
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

CollectionAssembly& CollectionAssembly::bind(CollectionBinder binder) {
  collection_.binder = std::move(binder);
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
