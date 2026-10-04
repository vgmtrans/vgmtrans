/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/CollectionModel.h"

#include <algorithm>

namespace vgmtrans::core {

bool Collection::contains(AssetId asset) const noexcept {
  const auto selected = [asset](const auto& ids) { return std::ranges::find(ids, asset) != ids.end(); };
  return selection.sequence == asset || selected(selection.soundBanks) || selected(selection.samplePools) ||
         selected(selection.miscAssets) || std::ranges::any_of(inputs.banks, [asset](const CollectionBank& bank) {
           return bank.bank == asset || std::ranges::find(bank.samples.targets, asset, &DependencyTarget::asset) !=
                                            bank.samples.targets.end();
         });
}

ResolutionStatus Collection::resolutionStatus() const noexcept {
  auto result = std::max(inputs.bankStatus, inputs.inspectionStatus);
  for (const auto& bank : inputs.banks) {
    result = std::max(result, bank.samples.status);
  }
  return result;
}

CollectionMembers Collection::members() const {
  auto result = selection;
  const auto include = [](std::vector<AssetId>& members, AssetId asset) {
    if (std::ranges::find(members, asset) == members.end()) {
      members.push_back(asset);
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

}  // namespace vgmtrans::core
