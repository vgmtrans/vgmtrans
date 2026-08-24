/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/SessionSnapshot.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

// Hands out session-unique IDs while scanners build values before admission.
// reserveAfter() keeps generated IDs ahead of explicit
// IDs that formats reserve for cross-references.
class ScanIdAllocator {
public:
  ScanIdAllocator() = default;
  ScanIdAllocator(ScanIdAllocator&& other) noexcept;
  ScanIdAllocator& operator=(ScanIdAllocator&& other) noexcept;
  ScanIdAllocator(const ScanIdAllocator&) = delete;
  ScanIdAllocator& operator=(const ScanIdAllocator&) = delete;

  [[nodiscard]] AssetId nextAssetId() noexcept;
  [[nodiscard]] CollectionId nextCollectionId() noexcept;
  [[nodiscard]] SourceAnnotationId nextSourceAnnotationId() noexcept;

  void reserveAfter(AssetId id) noexcept;
  void reserveAfter(CollectionId id) noexcept;
  void reserveAfter(SourceAnnotationId id) noexcept;

private:
  // Formats may assign IDs explicitly when they need cross-references. Generated
  // IDs always advance past any explicit IDs already seen.
  std::atomic<u32> nextAssetId_{0};
  std::atomic<u32> nextCollectionId_{0};
  std::atomic<u32> nextSourceAnnotationId_{0};
};

struct ScanInput {
  SourceFile source;
  ByteReader reader;
  ScanIdAllocator& ids;
  RetainedSource retained;

  // Session supplies retained storage. Direct scanner tests over borrowed
  // buffers take an explicit immutable snapshot only if a format needs it.
  [[nodiscard]] RetainedSource retain() const { return retained ? retained : RetainedSource::copyOf(reader); }
};

struct ExplicitCollection {
  CollectionKey key;
  std::string name;
  CollectionMembers members;
};

struct ScanResult {
  std::vector<Asset> assets;
  std::vector<ExplicitCollection> explicitCollections;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;
};

void normalizeScanResult(ScanResult& result, ScanIdAllocator& ids);

}  // namespace vgmtrans::core
