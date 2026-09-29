/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/CollectionModel.h"

#include <algorithm>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] CollectionIssue missingRoleIssue(std::string code, std::string role, std::optional<AssetId> asset) {
  return CollectionIssue{
      .severity = asset ? Severity::Error : Severity::Warning,
      .code = std::move(code),
      .message = asset ? "Collection references missing " + role + " asset " + std::to_string(asset->value)
                       : "Collection has no " + role + " asset",
      .asset = asset,
  };
}

}  // namespace

ResolutionStatus CollectionInputs::status() const noexcept {
  auto result = std::max(bankStatus, inspectionStatus);
  for (const auto& bank : banks) {
    result = std::max(result, bank.samples.status);
  }
  return result;
}

CollectionMembers collectionMembers(const CollectionMembers& selection, const CollectionInputs& inputs) {
  auto result = selection;
  const auto include = [](std::vector<AssetId>& members, AssetId id) {
    if (std::ranges::find(members, id) == members.end()) {
      members.push_back(id);
    }
  };
  for (const auto& bank : inputs.banks) {
    include(result.soundBanks, bank.bank);
    for (const auto& sample : bank.samples.targets) {
      include(result.samplePools, sample.asset);
    }
  }
  return result;
}

CollectionIssue missingSequenceIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sequence", "sequence", asset);
}

CollectionIssue missingSoundBankIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sound-bank", "sound bank", asset);
}

CollectionIssue missingSamplePoolIssue(std::optional<AssetId> asset) {
  return missingRoleIssue("missing-sample-pool", "sample pool", asset);
}

CollectionIssue ambiguousMatchIssue(std::string message, std::optional<AssetId> asset, SourceRange range) {
  return CollectionIssue{
      .severity = Severity::Warning,
      .code = "ambiguous-match",
      .message = std::move(message),
      .asset = asset,
      .range = range,
  };
}

}  // namespace vgmtrans::core
