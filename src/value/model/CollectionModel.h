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

// Explains a problem to the user. Severity controls how it is shown; the
// dependency's ResolutionStatus determines whether preparation can proceed.
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

// Listed from least to most serious; a collection reports the most serious
// outcome among its dependencies. Incomplete or ambiguous selections may still
// be usable, but Failed blocks preparation, regardless of diagnostic severity.
enum class ResolutionStatus { Resolved, Incomplete, Ambiguous, Failed };

// A chosen input asset, plus settings for this particular use. For example,
// placement can hold a bank number or the first sample to use in a pool.
// Settings must own their data because the catalog used for matching is temporary.
// Another sequence or bank can use the same asset with different settings.
struct DependencyTarget {
  AssetId asset;
  AssetPrivateData placement;
};

// owner is the sequence or bank requesting inputs; targets are its chosen inputs
// in order. All requests for the same role share this list and status. Alternatives
// record unresolved choices. Banks appear once, but a sample pool may appear at
// several starting positions.
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

// A collection description before the session assigns or reuses its CollectionId.
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
