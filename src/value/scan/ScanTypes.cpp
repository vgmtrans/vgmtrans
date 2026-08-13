/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace vgmtrans::core {

AssetId ScanIdAllocator::nextAssetId() noexcept {
  return AssetId{nextAssetId_++};
}

CollectionId ScanIdAllocator::nextCollectionId() noexcept {
  return CollectionId{nextCollectionId_++};
}

SourceAnnotationId ScanIdAllocator::nextSourceAnnotationId() noexcept {
  return SourceAnnotationId{nextSourceAnnotationId_++};
}

void ScanIdAllocator::reserveAfter(AssetId id) noexcept {
  if (id.valid()) {
    nextAssetId_ = std::max(nextAssetId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(CollectionId id) noexcept {
  if (id.valid()) {
    nextCollectionId_ = std::max(nextCollectionId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(SourceAnnotationId id) noexcept {
  if (id.valid()) {
    nextSourceAnnotationId_ = std::max(nextSourceAnnotationId_, id.value + 1);
  }
}

}  // namespace vgmtrans::core
