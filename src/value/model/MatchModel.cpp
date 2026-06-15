/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/MatchModel.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] CollectionIssue missingRoleIssue(std::string code, std::string role, std::optional<AssetId> asset) {
  if (asset) {
    return CollectionIssue{
        .severity = Severity::Error,
        .code = std::move(code),
        .message = "Collection references missing " + role + " asset " + std::to_string(asset->value),
        .asset = asset,
    };
  }

  return CollectionIssue{
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = "Collection has no " + role + " asset",
  };
}

[[nodiscard]] bool hasIssueCode(std::span<const CollectionIssue> issues, std::string_view code) {
  return std::ranges::any_of(issues, [code](const CollectionIssue& issue) { return issue.code == code; });
}

}  // namespace

CollectionIssue missingSequenceIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sequence", "sequence", asset);
}

CollectionIssue missingInstrumentSetIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-instrument-set", "instrument set", asset);
}

CollectionIssue missingSampleCollectionIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sample-collection", "sample collection", asset);
}

CollectionIssue ambiguousMatchIssue(std::string message, std::optional<AssetId> asset,
                                    std::optional<SourceRange> range) {
  return CollectionIssue{
      .severity = Severity::Warning,
      .code = "ambiguous-match",
      .message = std::move(message),
      .asset = asset,
      .range = range,
  };
}

CollectionIssue removedStaleAssetIssue(std::optional<AssetId> asset) {
  if (asset) {
    return CollectionIssue{
        .severity = Severity::Error,
        .code = "removed-asset",
        .message = "Collection references removed or stale asset " + std::to_string(asset->value),
        .asset = asset,
    };
  }

  return CollectionIssue{
      .severity = Severity::Error,
      .code = "removed-asset",
      .message = "Collection references an asset from a removed source",
  };
}

CollectionStatus validatedCollectionStatus(CollectionStatus status, std::span<const CollectionIssue> issues) {
  if (status != CollectionStatus::Complete) {
    return status;
  }

  if (hasIssueCode(issues, "removed-asset")) {
    return CollectionStatus::Stale;
  }
  if (hasIssueCode(issues, "ambiguous-match")) {
    return CollectionStatus::Ambiguous;
  }
  const bool hasIncompleteIssue = std::ranges::any_of(issues, [](const CollectionIssue& issue) {
    return issue.code.starts_with("missing-") || issue.severity == Severity::Error;
  });
  return hasIncompleteIssue ? CollectionStatus::Incomplete : CollectionStatus::Complete;
}

CollectionStatus validatedCollectionStatus(const DesiredCollection& collection) {
  return validatedCollectionStatus(collection.status, collection.issues);
}

}  // namespace vgmtrans::core
