/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionResolver.h"

#include <charconv>
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>

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

std::optional<MatchFieldValue> fieldValue(const FormatSpecificFact& fact, std::string_view name) {
  const auto found =
      std::ranges::find_if(fact.fields, [name](const MatchField& field) { return field.name == name; });
  if (found == fact.fields.end()) {
    return std::nullopt;
  }
  return MatchFieldValue{.value = found->value};
}

std::optional<u32> fieldU32(const FormatSpecificFact& fact, std::string_view name) {
  const auto value = fieldValue(fact, name);
  if (!value) {
    return std::nullopt;
  }

  u32 parsed = 0;
  const auto begin = value->value.data();
  const auto end = begin + value->value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed, 10);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<s32> fieldS32(const FormatSpecificFact& fact, std::string_view name) {
  const auto value = fieldValue(fact, name);
  if (!value) {
    return std::nullopt;
  }

  s32 parsed = 0;
  const auto begin = value->value.data();
  const auto end = begin + value->value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed, 10);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

bool fieldBool(const FormatSpecificFact& fact, std::string_view name, bool fallback) {
  const auto value = fieldValue(fact, name);
  if (!value) {
    return fallback;
  }
  return value->value == "1" || value->value == "true";
}

FormatSpecificFact formatFact(std::string kind, std::initializer_list<MatchField> fields) {
  return FormatSpecificFact{
      .kind = std::move(kind),
      .fields = std::vector<MatchField>(fields),
  };
}

MatchFactIndex::MatchFactIndex(const MatchContext& context) : context_(context) {
}

const SourceFile* MatchFactIndex::sourceFor(const MatchFact& fact) const {
  if (!fact.scope.source) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(context_.snapshot.sources(), [&](const SourceFile& source) {
    return source.id == *fact.scope.source;
  });
  return found == context_.snapshot.sources().end() ? nullptr : &*found;
}

CollectionAssembly::CollectionAssembly(CollectionKey key, std::string name)
    : collection_(DesiredCollection{
          .key = std::move(key),
          .name = std::move(name),
      }) {
}

CollectionAssembly& CollectionAssembly::sequence(AssetId id) {
  collection_.sequence = id;
  return *this;
}

CollectionAssembly& CollectionAssembly::instrumentSet(AssetId id) {
  addUnique(collection_.instrumentSets, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::sampleCollection(AssetId id) {
  addUnique(collection_.sampleCollections, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::misc(AssetId id) {
  addUnique(collection_.miscAssets, id);
  return *this;
}

CollectionAssembly& CollectionAssembly::issue(CollectionIssue issue) {
  collection_.issues.push_back(std::move(issue));
  return *this;
}

CollectionAssembly& CollectionAssembly::incomplete(CollectionIssue issue) {
  collection_.status = CollectionStatus::Incomplete;
  collection_.issues.push_back(std::move(issue));
  return *this;
}

CollectionAssembly& CollectionAssembly::ambiguous(std::string message, std::optional<AssetId> asset,
                                                  std::optional<SourceRange> range) {
  collection_.status = CollectionStatus::Ambiguous;
  collection_.issues.push_back(ambiguousMatchIssue(std::move(message), asset, range));
  return *this;
}

CollectionAssembly& CollectionAssembly::requireSequence() {
  if (!collection_.sequence) {
    incomplete(missingSequenceIssue());
  }
  return *this;
}

CollectionAssembly& CollectionAssembly::requireInstrumentSet() {
  if (collection_.instrumentSets.empty()) {
    incomplete(missingInstrumentSetIssue());
  }
  return *this;
}

CollectionAssembly& CollectionAssembly::requireSampleCollection() {
  if (collection_.sampleCollections.empty()) {
    incomplete(missingSampleCollectionIssue());
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

std::vector<DesiredCollection> resolveCollectionMemberFacts(const MatchContext& context, std::string_view resolver,
                                                            std::string_view format) {
  // Group member facts by CollectionKey so files loaded at different times can
  // still complete the same collection.
  std::map<std::pair<std::string, std::string>, DesiredCollection> grouped;

  for (const auto& fact : context.snapshot.matchFacts()) {
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
      collection.issues.push_back(missingSequenceIssue());
    }
    collections.push_back(std::move(collection));
  }
  return collections;
}

}  // namespace vgmtrans::core
