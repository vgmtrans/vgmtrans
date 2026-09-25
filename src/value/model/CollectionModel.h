/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/model/MetadataModel.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

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

// Ordered by precedence so a collection's status is its greatest issue impact.
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
  SourceRange range;
};

// The supported dependency chain is sequence -> sound bank -> sample pool.
enum class DependencyRole { SoundBank, SamplePool };

enum class ResolutionStatus { Resolved, Incomplete, Ambiguous, Failed };

// A selected provider and optional format-owned placement within it. Two banks
// may use the same pool at different positions; membership alone cannot express
// that relationship. Placements own values, never discovery-time pointers.
struct DependencyTarget {
  AssetId asset;
  AssetPrivateData placement;
};

struct ResolvedDependency {
  AssetId owner;
  DependencyRole role = DependencyRole::SoundBank;
  ResolutionStatus status = ResolutionStatus::Resolved;
  std::vector<DependencyTarget> targets;
  std::vector<DependencyTarget> alternatives;
};

// Opting into a collection is independent of whether a sequence needs banks.
// Empty identity/name fields use the sequence's asset identity and display name.
struct SequenceCollection {
  CollectionKey key;
  std::string name;
  // Supplemental inspection assets, not providers in the audio dependency chain.
  std::vector<AssetId> miscAssets;
};

struct DesiredCollection {
  // Stable identity within the resolver that produced this collection. The
  // session supplies the resolver namespace during reconciliation.
  std::string localKey;
  std::string name;
  CollectionMembers members;
  std::vector<CollectionIssue> issues;
  std::vector<ResolvedDependency> dependencies;
};

[[nodiscard]] CollectionIssue missingSequenceIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSoundBankIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSamplePoolIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue ambiguousMatchIssue(std::string message = "Collection has ambiguous matches",
                                                  std::optional<AssetId> asset = std::nullopt, SourceRange range = {});

}  // namespace vgmtrans::core
