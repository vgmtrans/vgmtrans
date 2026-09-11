/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

#include <algorithm>

namespace vgmtrans::core {

namespace {

void assignMissingAssetIds(std::vector<Asset>& assets, ScanIdAllocator& ids) {
  // Formats may assign IDs when assets reference each other. If they leave an ID
  // empty, assign one here before the asset leaves the scan result.
  for (auto& asset : assets) {
    auto& meta = metadata(asset);
    if (meta.id.valid()) {
      ids.reserveAfter(meta.id);
    } else {
      meta.id = ids.nextAssetId();
    }
  }
}

}  // namespace

void normalizeScanResult(ScanResult& result, ScanIdAllocator& ids) {
  assignMissingAssetIds(result.assets, ids);
}

}  // namespace vgmtrans::core
