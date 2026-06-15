/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionResolver.h"

#include <map>
#include <string>

namespace vgmtrans::core {

namespace {

void addMember(DesiredCollection& collection, AssetId asset, CollectionMemberRole role) {
  switch (role) {
    case CollectionMemberRole::Sequence:
      collection.sequence = asset;
      return;
    case CollectionMemberRole::InstrumentSet:
      collection.instrumentSets.push_back(asset);
      return;
    case CollectionMemberRole::SampleCollection:
      collection.sampleCollections.push_back(asset);
      return;
    case CollectionMemberRole::Misc:
      collection.miscAssets.push_back(asset);
      return;
  }
}

}  // namespace

std::vector<DesiredCollection> resolveCollectionMemberFacts(const MatchContext& context, std::string_view resolver,
                                                            std::string_view format) {
  // Group member facts by CollectionKey so files loaded at different times can
  // still complete the same collection.
  std::map<std::pair<std::string, std::string>, DesiredCollection> grouped;

  for (const auto& fact : context.snapshot.matchFacts) {
    if (!format.empty() && fact.format != format) {
      continue;
    }

    const auto* member = std::get_if<CollectionMemberFact>(&fact.payload);
    if (member == nullptr || member->key.resolver != resolver) {
      continue;
    }

    auto& collection = grouped[{member->key.resolver, member->key.value}];
    if (collection.key.resolver.empty()) {
      collection.key = member->key;
      collection.name = !member->collectionName.empty() ? member->collectionName : member->key.value;
    } else if (collection.name.empty() && !member->collectionName.empty()) {
      collection.name = member->collectionName;
    }

    addMember(collection, fact.asset, member->role);
  }

  std::vector<DesiredCollection> collections;
  collections.reserve(grouped.size());
  for (auto& entry : grouped) {
    auto& collection = entry.second;
    if (!collection.sequence) {
      collection.status = CollectionStatus::Incomplete;
    }
    collections.push_back(std::move(collection));
  }
  return collections;
}

}  // namespace vgmtrans::core
