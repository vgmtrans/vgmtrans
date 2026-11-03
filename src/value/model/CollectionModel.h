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

// Listed from least to most serious; a collection reports the most serious
// outcome among its dependencies. Incomplete or ambiguous selections may still
// be usable, but Failed blocks preparation, regardless of diagnostic severity.
enum class ResolutionStatus { Resolved, Incomplete, Ambiguous, Failed };

// A chosen input asset, plus settings for this particular use. For example,
// placement can hold the first sample to use in a pool.
// Settings must own their data because the catalog used for matching is temporary.
// Another sequence or bank can use the same asset with different settings.
struct DependencyTarget {
  AssetId asset;
  AssetPrivateData placement;
};

// The outcome of choosing an asset's inputs. The containing field determines
// their type. Sample uses retain repeated pools at different placements.
struct ResolvedInputs {
  ResolutionStatus status = ResolutionStatus::Resolved;
  std::vector<DependencyTarget> targets;
  std::vector<DependencyTarget> alternatives;
};

struct CollectionBank {
  AssetId bank;
  ResolvedInputs samples;
};

// Resolution stores the actual audio relationships once. Manual choices remain
// in CollectionMembers; they can include pools that no bank needs.
struct CollectionInputs {
  std::vector<CollectionBank> banks;
  ResolutionStatus bankStatus = ResolutionStatus::Resolved;
  std::vector<DependencyTarget> bankAlternatives;
  ResolutionStatus inspectionStatus = ResolutionStatus::Resolved;

  [[nodiscard]] ResolutionStatus status() const noexcept;
};

// Flat inspection/export membership is a view, not a second list to synchronize.
// Explicit choices keep their order; automatically used assets follow once each.
[[nodiscard]] CollectionMembers collectionMembers(const CollectionMembers& selection, const CollectionInputs& inputs);

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
  CollectionMembers selection;
  std::vector<CollectionIssue> issues;
  CollectionInputs inputs;

  [[nodiscard]] CollectionMembers members() const { return collectionMembers(selection, inputs); }
};

[[nodiscard]] CollectionIssue missingSequenceIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSoundBankIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue missingSamplePoolIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] CollectionIssue ambiguousMatchIssue(std::string message = "Collection has ambiguous matches",
                                                  std::optional<AssetId> asset = std::nullopt, SourceRange range = {});

}  // namespace vgmtrans::core
