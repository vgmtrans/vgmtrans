/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/SessionSnapshot.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

// Hands out session-unique IDs while scanners build value
// objects before commit. reserveAfter() keeps generated IDs ahead of explicit
// IDs that formats reserve for cross-references.
class ScanIdAllocator {
public:
  [[nodiscard]] AssetId nextAssetId() noexcept;
  [[nodiscard]] CollectionId nextCollectionId() noexcept;
  [[nodiscard]] SourceAnnotationId nextSourceAnnotationId() noexcept;

  void reserveAfter(AssetId id) noexcept;
  void reserveAfter(CollectionId id) noexcept;
  void reserveAfter(SourceAnnotationId id) noexcept;

private:
  // Formats may assign IDs explicitly when they need cross-references. Generated
  // IDs always advance past any explicit IDs already seen.
  u32 nextAssetId_ = 0;
  u32 nextCollectionId_ = 0;
  u32 nextSourceAnnotationId_ = 0;
};

struct ScanInput {
  SourceFile source;
  ByteReader reader;
  ScanIdAllocator& ids;
};

struct ExplicitCollection {
  CollectionKey key;
  std::string name;
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentSets;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
};

struct ScanResult {
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<ExplicitCollection> explicitCollections;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;
  std::vector<ExtractedSource> extractedSources;
};

void normalizeScanResult(ScanResult& result, ScanIdAllocator& ids);

}  // namespace vgmtrans::core
