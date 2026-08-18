/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct CollectionBindingContext;
using CollectionBinder = std::function<void(CollectionBindingContext&)>;

struct CollectionKey {
  // Stable identity for a resolved collection. The same key updates the same
  // collection when more sources are loaded later.
  std::string resolver;
  std::string value;

  friend bool operator==(const CollectionKey&, const CollectionKey&) noexcept = default;
};

struct CollectionMembers {
  std::optional<AssetId> sequence;
  std::vector<AssetId> soundBanks;
  std::vector<AssetId> samplePools;
  std::vector<AssetId> miscAssets;
};

enum class CollectionFreshness {
  Current,
  Stale,
};

enum class CollectionResolution {
  Resolved,
  Incomplete,
  Ambiguous,
};

enum class CollectionOrigin {
  Discovered,
  UserCreated,
};

enum class CollectionIssueImpact {
  None,
  Incomplete,
  Ambiguous,
};

struct CollectionIssue {
  CollectionIssueImpact impact = CollectionIssueImpact::None;
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  std::optional<AssetId> asset;
  std::optional<SourceRange> range;
};

struct DesiredCollection {
  CollectionKey key;
  std::string name;
  CollectionMembers members;
  std::vector<CollectionIssue> issues;
  // Preserves resolver-specific decisions that member lists cannot express.
  // Reconciliation uses the resolver's default binder when this is empty.
  CollectionBinder binder;
};

[[nodiscard]] CollectionIssue missingSequenceIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSoundBankIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSamplePoolIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue ambiguousMatchIssue(std::string message = "Collection has ambiguous matches",
                                                  std::optional<AssetId> asset = std::nullopt,
                                                  std::optional<SourceRange> range = std::nullopt);
[[nodiscard]] CollectionIssue removedStaleAssetIssue(std::optional<AssetId> asset = std::nullopt);

// Resolution summarizes the semantic effect of the issues. Ambiguity takes
// precedence when a collection has more than one kind of problem.
[[nodiscard]] CollectionResolution collectionResolution(std::span<const CollectionIssue> issues) noexcept;

}  // namespace vgmtrans::core
