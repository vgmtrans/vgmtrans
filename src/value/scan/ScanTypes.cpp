/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

namespace vgmtrans::core {

ScanIdAllocator::ScanIdAllocator(ScanIdAllocator&& other) noexcept
    : nextAssetId_(other.nextAssetId_.load(std::memory_order_relaxed)),
      nextCollectionId_(other.nextCollectionId_.load(std::memory_order_relaxed)),
      nextSourceAnnotationId_(other.nextSourceAnnotationId_.load(std::memory_order_relaxed)) {
}

ScanIdAllocator& ScanIdAllocator::operator=(ScanIdAllocator&& other) noexcept {
  nextAssetId_.store(other.nextAssetId_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  nextCollectionId_.store(other.nextCollectionId_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  nextSourceAnnotationId_.store(other.nextSourceAnnotationId_.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
  return *this;
}

AssetId ScanIdAllocator::nextAssetId() noexcept {
  return AssetId{nextAssetId_.fetch_add(1, std::memory_order_relaxed)};
}

CollectionId ScanIdAllocator::nextCollectionId() noexcept {
  return CollectionId{nextCollectionId_.fetch_add(1, std::memory_order_relaxed)};
}

SourceAnnotationId ScanIdAllocator::nextSourceAnnotationId() noexcept {
  return SourceAnnotationId{nextSourceAnnotationId_.fetch_add(1, std::memory_order_relaxed)};
}

}  // namespace vgmtrans::core
