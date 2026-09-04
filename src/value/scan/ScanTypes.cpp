/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanTypes.h"

namespace vgmtrans::core {

namespace {

void advancePast(std::atomic<u32>& next, u32 id) noexcept {
  const u32 minimum = id + 1;
  u32 current = next.load(std::memory_order_relaxed);
  while (current < minimum &&
         !next.compare_exchange_weak(current, minimum, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
}

}  // namespace

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

void ScanIdAllocator::reserveAfter(AssetId id) noexcept {
  if (id.valid()) {
    advancePast(nextAssetId_, id.value);
  }
}

void ScanIdAllocator::reserveAfter(CollectionId id) noexcept {
  if (id.valid()) {
    advancePast(nextCollectionId_, id.value);
  }
}

void ScanIdAllocator::reserveAfter(SourceAnnotationId id) noexcept {
  if (id.valid()) {
    advancePast(nextSourceAnnotationId_, id.value);
  }
}

}  // namespace vgmtrans::core
