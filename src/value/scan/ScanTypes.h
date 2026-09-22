/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/SessionSnapshot.h"

#include <atomic>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Thread-safe session-unique IDs for scanners building values before admission.
// Allocate IDs before constructing assets and their references, either directly
// through ScanInput::ids or through ScanResultBuilder.
class ScanIdAllocator {
public:
  [[nodiscard]] AssetId nextAssetId() noexcept {
    return AssetId{nextAssetId_.fetch_add(1, std::memory_order_relaxed)};
  }
  [[nodiscard]] SourceAnnotationId nextSourceAnnotationId() noexcept {
    return SourceAnnotationId{nextSourceAnnotationId_.fetch_add(1, std::memory_order_relaxed)};
  }

private:
  std::atomic<u32> nextAssetId_{0};
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

}  // namespace vgmtrans::core
