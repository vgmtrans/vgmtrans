/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ScanTypes.h"
#include "value/session/AssetStore.h"
#include "value/session/DiagnosticStore.h"

#include <string_view>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Owns discovered/user collections and applies resolver output. Resolver-owned
// collections are reconciled by key: same key updates in place, missing key removes
// the old discovered collection, and resolver failures leave old collections visible.
class CollectionStore {
public:
  [[nodiscard]] const std::vector<Collection>& all() const noexcept { return collections_; }

  void reconcile(std::string_view resolverId, std::vector<DesiredCollection> desiredCollections,
                 const AssetStore& assets, DiagnosticStore& diagnostics, ScanIdAllocator& ids);
  void markStaleForAssets(const std::unordered_set<u32>& assetIds);

private:
  [[nodiscard]] CollectionId nextCollectionId(ScanIdAllocator& ids);
  void validateAssetReferences(std::string_view resolverId, DesiredCollection& desired, const AssetStore& assets,
                               DiagnosticStore& diagnostics);

  std::vector<Collection> collections_;
};

}  // namespace vgmtrans::core
