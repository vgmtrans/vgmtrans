/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/SessionSnapshot.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

class ScanIdAllocator {
public:
  [[nodiscard]] AssetId nextAssetId() noexcept;
  [[nodiscard]] CollectionId nextCollectionId() noexcept;
  [[nodiscard]] ItemId nextItemId() noexcept;

  void reserveAfter(AssetId id) noexcept;
  void reserveAfter(CollectionId id) noexcept;
  void reserveAfter(ItemId id) noexcept;

private:
  // Formats may assign IDs explicitly when they need cross-references. Generated
  // IDs always advance past any explicit IDs already seen.
  u32 nextAssetId_ = 0;
  u32 nextCollectionId_ = 0;
  u32 nextItemId_ = 0;
};

struct ScanInput {
  SourceFile source;
  ByteReader reader;
  ScanIdAllocator& ids;
};

class ItemTreeBuilder {
public:
  ItemTreeBuilder(ItemTree& tree, ScanIdAllocator& ids);

  // Adds a UI item and updates the parent's child list at the same time.
  [[nodiscard]] ItemId add(std::optional<ItemId> parent, ItemKind kind, std::string detailKind, std::string name,
                           SourceRange range, std::string description = {});

private:
  ItemTree& tree_;
  ScanIdAllocator& ids_;
};

struct ScanResult {
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<Diagnostic> diagnostics;
  std::vector<ExtractedSource> extractedSources;
};

void normalizeScanResult(ScanResult& result, ScanIdAllocator& ids);

}  // namespace vgmtrans::core
