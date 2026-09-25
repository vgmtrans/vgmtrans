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

struct CollectionMembers {
  std::optional<AssetId> sequence;
  std::vector<AssetId> soundBanks;
  std::vector<AssetId> samplePools;
  std::vector<AssetId> miscAssets;
};

// Diagnostic presentation is independent of dependency usability.
struct CollectionIssue {
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  std::optional<AssetId> asset;
  SourceRange range;
};

// Audio dependencies form sequence -> sound bank -> sample pool. Supplemental
// references connect a sequence to inspection assets outside that audio chain.
enum class DependencyRole { SoundBank, SamplePool, Supplemental };

// Ordered by precedence. Each relationship owns its outcome; collection status
// is the greatest of those outcomes, independent of diagnostic severity.
// Incomplete and ambiguous inputs may still be usable; failed resolution blocks preparation.
enum class ResolutionStatus { Resolved, Incomplete, Ambiguous, Failed };

// A selected provider and optional format-owned placement within it. Two banks
// may use the same pool at different positions; membership alone cannot express
// that relationship. Placements own values, never discovery-time pointers.
struct DependencyTarget {
  AssetId asset;
  AssetPrivateData placement;
};

// One ordered input list per owner and role. Status combines all requests;
// diagnostics and alternatives retain unresolved choices. Bank targets are
// unique by asset; sample targets may repeat an asset at different placements.
struct ResolvedDependency {
  AssetId owner;
  DependencyRole role = DependencyRole::SoundBank;
  ResolutionStatus status = ResolutionStatus::Resolved;
  std::vector<DependencyTarget> targets;
  std::vector<DependencyTarget> alternatives;
};

// Published sequences produce collections by default. Identity is always the
// sequence's AssetId; an empty name uses the sequence's display name.
struct SequenceCollectionOptions {
  bool enabled = true;
  std::string name;
  // Supplemental inspection assets, not providers in the audio dependency chain.
  std::vector<AssetId> miscAssets;
};

struct DesiredCollection {
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
