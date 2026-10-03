/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/model/MetadataModel.h"

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

enum class CollectionOrigin { Discovered, User };

// Session assigns or preserves the ID and origin when publishing a collection.
struct Collection {
  CollectionId id;
  std::string name;
  // A discovered collection is identified by selection.sequence during rebuilds.
  // User collections have independent identities, even for the same sequence.
  CollectionOrigin origin = CollectionOrigin::User;
  // Collections are the export units. A sequence may be paired with instrument
  // banks and sample pools loaded from the same or separate sources.
  // Original choices: automatic collections retain only their root and inspection assets.
  CollectionMembers selection;
  // Messages may refer to missing assets. ResolutionStatus controls preparation;
  // diagnostic severity controls presentation.
  std::vector<Diagnostic> issues;
  CollectionInputs inputs;

  [[nodiscard]] CollectionMembers members() const { return collectionMembers(selection, inputs); }
  [[nodiscard]] bool contains(AssetId asset) const noexcept;

  [[nodiscard]] bool isDiscovered() const noexcept { return origin == CollectionOrigin::Discovered; }
  [[nodiscard]] ResolutionStatus resolutionStatus() const noexcept { return inputs.status(); }
};

[[nodiscard]] Diagnostic missingSequenceIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] Diagnostic missingSoundBankIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] Diagnostic missingSamplePoolIssue(std::optional<AssetId> asset = std::nullopt);
[[nodiscard]] Diagnostic ambiguousMatchIssue(std::string message = "Collection has ambiguous matches",
                                            std::optional<AssetId> asset = std::nullopt, SourceRange range = {});

}  // namespace vgmtrans::core
