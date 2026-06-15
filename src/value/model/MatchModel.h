/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// Scanners emit MatchFact records when assets may belong together. Collection
// resolvers read the accumulated facts and decide which collections should exist.

enum class MatchScopeKind {
  Session,
  Source,
};

struct CollectionKey {
  // Stable identity for a resolved collection. The same key updates the same
  // collection when more sources are loaded later.
  std::string resolver;
  std::string value;

  friend bool operator==(const CollectionKey&, const CollectionKey&) noexcept = default;
};

enum class CollectionMemberRole {
  Sequence,
  InstrumentSet,
  SampleCollection,
  Misc,
};

struct MatchScope {
  MatchScopeKind kind = MatchScopeKind::Session;
  std::optional<SourceId> source;

  friend bool operator==(const MatchScope&, const MatchScope&) noexcept = default;
};

struct IdMatchFact {
  std::string domain;
  u32 value = 0;
};

struct FilenameStemFact {
  std::string stem;
};

struct OffsetOrderFact {
  u64 offset = 0;
};

struct SampleCoverageFact {
  u32 first = 0;
  u32 count = 0;
};

struct SampleRequirementFact {
  std::vector<u32> required;
};

struct CollectionMemberFact {
  // Simple case: the scanner already knows this asset belongs to this collection.
  CollectionKey key;
  std::string collectionName;
  CollectionMemberRole role = CollectionMemberRole::Misc;
};

struct MatchField {
  std::string name;
  std::string value;
};

struct FormatSpecificFact {
  std::string kind;
  std::vector<MatchField> fields;
};

using MatchFactPayload = std::variant<IdMatchFact, FilenameStemFact, OffsetOrderFact, SampleCoverageFact,
                                      SampleRequirementFact, CollectionMemberFact, FormatSpecificFact>;

struct MatchFact {
  AssetId asset;
  std::string format;
  MatchScope scope;
  MatchFactPayload payload;
};

enum class CollectionStatus {
  Complete,
  Incomplete,
};

struct DesiredCollection {
  CollectionKey key;
  std::string name;
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
  CollectionStatus status = CollectionStatus::Complete;
};

}  // namespace vgmtrans::core
