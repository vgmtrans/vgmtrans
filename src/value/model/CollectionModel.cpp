/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/CollectionModel.h"

#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] CollectionIssue missingRoleIssue(std::string code, std::string role, std::optional<AssetId> asset) {
  if (asset) {
    return CollectionIssue{
        .impact = CollectionIssueImpact::Incomplete,
        .severity = Severity::Error,
        .code = std::move(code),
        .message = "Collection references missing " + role + " asset " + std::to_string(asset->value),
        .asset = asset,
    };
  }

  return CollectionIssue{
      .impact = CollectionIssueImpact::Incomplete,
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = "Collection has no " + role + " asset",
  };
}

}  // namespace

CollectionIssue missingSequenceIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sequence", "sequence", asset);
}

CollectionIssue missingSoundBankIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sound-bank", "sound bank", asset);
}

CollectionIssue missingSamplePoolIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sample-pool", "sample pool", asset);
}

CollectionIssue ambiguousMatchIssue(std::string message, std::optional<AssetId> asset,
                                    std::optional<SourceRange> range) {
  return CollectionIssue{
      .impact = CollectionIssueImpact::Ambiguous,
      .severity = Severity::Warning,
      .code = "ambiguous-match",
      .message = std::move(message),
      .asset = asset,
      .range = range,
  };
}

CollectionResolution collectionResolution(std::span<const CollectionIssue> issues) noexcept {
  CollectionResolution resolution = CollectionResolution::Resolved;
  for (const auto& issue : issues) {
    if (issue.impact == CollectionIssueImpact::Ambiguous) {
      return CollectionResolution::Ambiguous;
    }
    if (issue.impact == CollectionIssueImpact::Incomplete) {
      resolution = CollectionResolution::Incomplete;
    }
  }
  return resolution;
}

}  // namespace vgmtrans::core
